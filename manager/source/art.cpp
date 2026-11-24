/*
 * Copyright (c) 2026 NedcloarBR
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <art.hpp>
#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <list>
#include <sys/stat.h>
#include <unistd.h>
#include <map>
#include <mutex>
#include <curl/curl.h>
#include <SDL2/SDL_image.h>
#include <gametdb.hpp>
#include <manager_settings.hpp>
#include <sd_files.hpp>
#include <usbl/version.hpp>

namespace art {

    namespace {

        constexpr char EnabledKey[] = "gametdb_art";
        constexpr char CertificatePath[] = "romfs:/certs/gametdb.crt";
        constexpr char IdsSection[] = "ids";
        constexpr char MissingSection[] = "missing";
        /* A file GameTDB does not have, or a game it does not know, is asked for again after this long. */
        constexpr time_t RetryAfter = 7 * 24 * 60 * 60;
        /* The front cover's share of a Spread's height (1166 x 1888 at full size): front and back are this wide. */
        constexpr double CoverAspect = 1166.0 / 1888.0;

        struct KindInfo {
            const char *folder;
            const char *gametdb;
            const char *extension;
        };
        constexpr KindInfo Kinds[] = {
            { "cover", "coverM", "jpg" },
            { "box", "box", "png" },
            { "spread", "coverfullM", "jpg" },
            { "cart", "cart", "png" },
        };
        static_assert(std::size(Kinds) == static_cast<size_t>(Kind::Count));

        std::string TitleId(u64 application_id) {
            char text[17];
            std::snprintf(text, sizeof(text), "%016" PRIX64, application_id);
            return text;
        }

        std::string ArtPath(u64 application_id, Kind kind) {
            const KindInfo &info = Kinds[static_cast<size_t>(kind)];
            return sd::Path(usbl::paths::ArtCacheDir) + "/" + info.folder + "/" + TitleId(application_id) + "." + info.extension;
        }

        std::string StatePath() {
            return sd::Path(usbl::paths::ArtCacheDir) + "/state.ini";
        }

        std::mutex g_mutex;
        std::condition_variable g_wake;
        std::vector<Game> g_games;
        bool g_games_changed = false;
        bool g_retry = false;
        bool g_stop = false;
        /* Set by DeleteAll: the thread stops its pass and deletes. */
        bool g_delete = false;
        Status g_status = {};
        Report g_report = {};
        /* The render thread drops its textures (the files are gone). */
        std::atomic<bool> g_textures_stale = false;
        std::atomic<u32> g_generation = 1;

        Thread g_thread;
        bool g_thread_started = false;
        bool g_network_ready = false;

        /* Remembered between runs (cache/art/state.ini): which GameTDB entry each game is, and what was missing when. */
        struct State {
            std::map<std::string, std::string> ids;
            std::map<std::string, time_t> missing;
        };

        State ReadState() {
            State state;
            sd::IniFile file;
            if (!sd::ReadIni(&file, StatePath())) {
                return state;
            }
            if (const auto *ids = file.FindSection(IdsSection); ids != nullptr) {
                for (const auto &[key, value] : *ids) {
                    state.ids[key] = value;
                }
            }
            if (const auto *missing = file.FindSection(MissingSection); missing != nullptr) {
                for (const auto &[key, value] : *missing) {
                    state.missing[key] = static_cast<time_t>(std::strtoll(value.c_str(), nullptr, 10));
                }
            }
            return state;
        }

        void WriteState(const State &state) {
            sd::IniFile file;
            auto &ids = file.Section(IdsSection);
            for (const auto &[key, value] : state.ids) {
                ids.push_back({ key, value });
            }
            auto &missing = file.Section(MissingSection);
            for (const auto &[key, value] : state.missing) {
                missing.push_back({ key, std::to_string(static_cast<long long>(value)) });
            }
            sd::WriteIni(file, 1, StatePath());
        }

        bool IsStillMissing(const State &state, const std::string &key) {
            const auto found = state.missing.find(key);
            return found != state.missing.end() && std::time(nullptr) - found->second < RetryAfter;
        }

        bool HasInternet() {
            if (R_FAILED(nifmInitialize(NifmServiceType_User))) {
                return false;
            }
            NifmInternetConnectionType type;
            u32 strength = 0;
            NifmInternetConnectionStatus status;
            const bool connected = R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &status)) && status == NifmInternetConnectionStatus_Connected;
            nifmExit();
            return connected;
        }

        bool PrepareNetwork() {
            if (g_network_ready) {
                return true;
            }
            if (R_FAILED(socketInitializeDefault())) {
                return false;
            }
            if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
                socketExit();
                return false;
            }
            g_network_ready = true;
            return true;
        }

        size_t WriteToBuffer(char *data, size_t size, size_t count, void *buffer) {
            auto *out = static_cast<std::vector<u8> *>(buffer);
            out->insert(out->end(), data, data + size * count);
            return size * count;
        }

        enum class Fetch {
            Saved,
            NotFound,
            Failed,
        };

        /* One connection for a whole pass: each new HTTPS connection costs a TLS handshake, slow on the console. */
        class Downloader {
            private:
                CURL *curl;
                std::vector<u8> buffer;
                std::string agent = std::string("USBLoaderNX-Manager/") + usbl::Version;

            public:
                Downloader() : curl(curl_easy_init()) {
                    if (this->curl == nullptr) {
                        return;
                    }
                    curl_easy_setopt(this->curl, CURLOPT_USERAGENT, this->agent.c_str());
                    curl_easy_setopt(this->curl, CURLOPT_CAINFO, CertificatePath);
                    curl_easy_setopt(this->curl, CURLOPT_FOLLOWLOCATION, 0L);
                    curl_easy_setopt(this->curl, CURLOPT_CONNECTTIMEOUT, 10L);
                    curl_easy_setopt(this->curl, CURLOPT_TIMEOUT, 30L);
                    curl_easy_setopt(this->curl, CURLOPT_MAXFILESIZE, 8L * 1024 * 1024);
                    curl_easy_setopt(this->curl, CURLOPT_TCP_KEEPALIVE, 1L);
                    curl_easy_setopt(this->curl, CURLOPT_WRITEFUNCTION, WriteToBuffer);
                    curl_easy_setopt(this->curl, CURLOPT_WRITEDATA, &this->buffer);
                }

                ~Downloader() {
                    if (this->curl != nullptr) {
                        curl_easy_cleanup(this->curl);
                    }
                }

                Downloader(const Downloader &) = delete;
                Downloader &operator=(const Downloader &) = delete;

                /* `url` into `path`, written in one go once it is complete (a broken download never looks like art). */
                Fetch Get(const std::string &url, const std::string &path) {
                    if (this->curl == nullptr) {
                        return Fetch::Failed;
                    }
                    this->buffer.clear();
                    curl_easy_setopt(this->curl, CURLOPT_URL, url.c_str());
                    const CURLcode result = curl_easy_perform(this->curl);
                    long code = 0;
                    curl_easy_getinfo(this->curl, CURLINFO_RESPONSE_CODE, &code);
                    if (result != CURLE_OK || code != 200 || this->buffer.empty()) {
                        return result == CURLE_OK && code == 404 ? Fetch::NotFound : Fetch::Failed;
                    }
                    const std::string partial = path + ".part";
                    FILE *file = std::fopen(partial.c_str(), "wb");
                    if (file == nullptr) {
                        return Fetch::Failed;
                    }
                    const bool written = std::fwrite(this->buffer.data(), 1, this->buffer.size(), file) == this->buffer.size();
                    if (std::fclose(file) == 0 && written) {
                        std::remove(path.c_str());
                        if (std::rename(partial.c_str(), path.c_str()) == 0) {
                            return Fetch::Saved;
                        }
                    }
                    std::remove(partial.c_str());
                    return Fetch::Failed;
                }
        };

        bool Stopping() {
            std::scoped_lock lock(g_mutex);
            return g_stop || g_delete;
        }

        struct Job {
            size_t game;
            Kind kind;
        };

        /* One pass over the games: covers for all of them first (what most views show), then cases, opened covers and
         * game cards. */
        void DownloadMissing(const std::vector<Game> &games, bool retry) {
            State state = ReadState();
            if (retry) {
                state.missing.clear();
            }
            {
                std::scoped_lock lock(g_mutex);
                g_status = { true, false, 0, 0 };
            }

            /* Which GameTDB entry each game is: the player's own choice (data/gametdb.ini) or the index; a game
             * matched before keeps its entry when its names are gone (no control data). Another entry than before:
             * its art goes. */
            std::vector<gametdb::Entry> entries(games.size());
            std::vector<bool> matched(games.size(), false);
            std::vector<Job> jobs;
            for (size_t i = 0; i < games.size(); ++i) {
                const Game &game = games[i];
                const std::string title_id = TitleId(game.application_id);
                gametdb::Entry entry;
                bool found = gametdb::Find(game.application_id, game.names, &entry);
                const auto known = state.ids.find(title_id);
                if (found && known != state.ids.end() && known->second != entry.id) {
                    for (size_t k = 0; k < std::size(Kinds); ++k) {
                        std::remove(ArtPath(game.application_id, static_cast<Kind>(k)).c_str());
                        state.missing.erase(title_id + "." + Kinds[k].folder);
                    }
                    ++g_generation;
                }
                if (found) {
                    state.ids[title_id] = entry.id;
                } else if (known != state.ids.end()) {
                    entry = { known->second, {} };
                    found = true;
                }
                entries[i] = entry;
                matched[i] = found;
            }
            for (size_t k = 0; k < std::size(Kinds); ++k) {
                for (size_t i = 0; i < games.size(); ++i) {
                    const std::string key = TitleId(games[i].application_id) + "." + Kinds[k].folder;
                    if (matched[i] && !sd::Exists(ArtPath(games[i].application_id, static_cast<Kind>(k))) && !IsStillMissing(state, key)) {
                        jobs.push_back({ i, static_cast<Kind>(k) });
                    }
                }
            }
            {
                std::scoped_lock lock(g_mutex);
                g_status.total = static_cast<u32>(jobs.size());
            }

            bool offline = false;
            if (!jobs.empty()) {
                offline = !(HasInternet() && PrepareNetwork());
            }
            if (!offline && !jobs.empty()) {
                Downloader downloader;
                /* The art region that worked for a game goes first for its other art. */
                std::vector<std::string> region_hint(games.size());
                for (const Job &job : jobs) {
                    if (Stopping()) {
                        break;
                    }
                    const Game &game = games[job.game];
                    const KindInfo &info = Kinds[static_cast<size_t>(job.kind)];
                    sd::MakeDirectories(sd::Path(usbl::paths::ArtCacheDir) + "/" + info.folder);
                    std::vector<std::string> regions = gametdb::ArtRegions(entries[job.game]);
                    if (const std::string &hint = region_hint[job.game]; !hint.empty()) {
                        regions.erase(std::remove(regions.begin(), regions.end(), hint), regions.end());
                        regions.insert(regions.begin(), hint);
                    }
                    Fetch fetched = Fetch::NotFound;
                    for (const auto &region : regions) {
                        const std::string url = std::string("https://art.gametdb.com/switch/") + info.gametdb + "/" + region + "/" + entries[job.game].id + "." + info.extension;
                        fetched = downloader.Get(url, ArtPath(game.application_id, job.kind));
                        if (fetched != Fetch::NotFound) {
                            if (fetched == Fetch::Saved) {
                                region_hint[job.game] = region;
                            }
                            break;
                        }
                    }
                    if (fetched == Fetch::Saved) {
                        ++g_generation;
                    } else if (fetched == Fetch::NotFound) {
                        state.missing[TitleId(game.application_id) + "." + info.folder] = std::time(nullptr);
                    }
                    std::scoped_lock lock(g_mutex);
                    ++g_status.done;
                }
            }
            WriteState(state);

            /* What the art window shows: which games have a cover, and why the others do not. */
            std::vector<MissingArt> missing;
            u32 with_art = 0;
            for (size_t i = 0; i < games.size(); ++i) {
                if (sd::Exists(ArtPath(games[i].application_id, Kind::Cover))) {
                    ++with_art;
                    continue;
                }
                const std::string key = TitleId(games[i].application_id) + "." + Kinds[0].folder;
                const Missing reason = !matched[i] ? Missing::NotOnGameTdb
                                     : state.missing.count(key) != 0 ? Missing::NotOnServer
                                     : offline ? Missing::NoInternet
                                     : Missing::Failed;
                missing.push_back({ games[i].name, reason });
            }
            std::scoped_lock lock(g_mutex);
            g_status.running = false;
            g_status.offline = offline;
            g_report = { with_art, static_cast<u32>(games.size()), std::move(missing) };
        }

        /* Everything downloaded goes, with what was remembered about it. */
        void DeleteArt() {
            for (const auto &info : Kinds) {
                const std::string folder = sd::Path(usbl::paths::ArtCacheDir) + "/" + info.folder;
                if (DIR *dir = opendir(folder.c_str()); dir != nullptr) {
                    while (const dirent *entry = readdir(dir)) {
                        if (entry->d_name[0] != '.') {
                            std::remove((folder + "/" + entry->d_name).c_str());
                        }
                    }
                    closedir(dir);
                    rmdir(folder.c_str());
                }
            }
            std::remove(StatePath().c_str());
            g_textures_stale = true;
            ++g_generation;
            std::scoped_lock lock(g_mutex);
            g_report = {};
            g_status = {};
        }

        void Run(void *) {
            while (true) {
                std::vector<Game> games;
                bool retry = false, remove = false;
                {
                    std::unique_lock lock(g_mutex);
                    g_wake.wait(lock, [] { return g_stop || g_games_changed || g_retry || g_delete; });
                    if (g_stop) {
                        return;
                    }
                    games = g_games;
                    retry = g_retry;
                    remove = g_delete;
                    g_games_changed = false;
                    g_retry = false;
                }
                if (remove) {
                    DeleteArt();
                    std::scoped_lock lock(g_mutex);
                    g_delete = false;
                    continue;
                }
                DownloadMissing(games, retry);
            }
        }

        void StartThread() {
            if (g_thread_started) {
                return;
            }
            constexpr size_t Stack = 128 * 1024;
            for (const int core : { 1, 2, -2 }) {
                if (R_SUCCEEDED(threadCreate(&g_thread, Run, nullptr, nullptr, Stack, 0x3B, core))) {
                    if (R_SUCCEEDED(threadStart(&g_thread))) {
                        g_thread_started = true;
                        return;
                    }
                    threadClose(&g_thread);
                }
            }
        }

        void Wake() {
            if (!IsEnabled()) {
                return;
            }
            StartThread();
            g_wake.notify_all();
        }

        /* Loaded art, least recently used last. */
        struct CachedTexture {
            u64 application_id;
            Kind kind;
            pu::sdl2::TextureHandle::Ref texture;
            /* Null texture: not on the SD card at this generation (looked again after a new file arrives). */
            u32 generation;
        };
        std::list<CachedTexture> g_textures;
        constexpr size_t MaxTextures = 48;

        /* The spine of a Spread image: between the back and the front, which are as wide as a cover for its height. */
        SDL_Rect SpineOf(int width, int height) {
            const int side = std::min(width / 2, static_cast<int>(height * CoverAspect));
            return { side, 0, std::max(1, width - 2 * side), height };
        }

        /* Textures are cached by kind; a spine is cached apart from its Spread. */
        constexpr Kind SpineKind = Kind::Count;

        SDL_Texture *LoadSpine(const std::string &path) {
            SDL_Surface *spread = IMG_Load(path.c_str());
            if (spread == nullptr) {
                return nullptr;
            }
            SDL_Rect spine = SpineOf(spread->w, spread->h);
            SDL_Texture *texture = nullptr;
            if (SDL_Surface *cut = SDL_CreateRGBSurfaceWithFormat(0, spine.w, spine.h, 32, SDL_PIXELFORMAT_RGBA32); cut != nullptr) {
                SDL_BlitSurface(spread, &spine, cut, nullptr);
                texture = SDL_CreateTextureFromSurface(pu::ui::render::GetMainRenderer(), cut);
                SDL_FreeSurface(cut);
            }
            SDL_FreeSurface(spread);
            return texture;
        }

        SDL_Rect TextureSize(SDL_Texture *texture) {
            int width = 0, height = 0;
            SDL_QueryTexture(texture, nullptr, nullptr, &width, &height);
            return { 0, 0, width, height };
        }

    }

    bool IsEnabled() {
        return settings::GetInt(EnabledKey, 0) != 0;
    }

    bool SetEnabled(bool enabled) {
        if (!settings::SetInt(EnabledKey, enabled ? 1 : 0)) {
            return false;
        }
        if (enabled) {
            {
                std::scoped_lock lock(g_mutex);
                g_games_changed = true;
            }
            Wake();
        }
        return true;
    }

    void SetGames(std::vector<Game> games) {
        {
            std::scoped_lock lock(g_mutex);
            const auto same = [](const std::vector<Game> &a, const std::vector<Game> &b) {
                return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const Game &x, const Game &y) { return x.application_id == y.application_id; });
            };
            if (same(g_games, games)) {
                return;
            }
            g_games = std::move(games);
            g_games_changed = true;
        }
        Wake();
    }

    void Retry() {
        {
            std::scoped_lock lock(g_mutex);
            g_retry = true;
        }
        Wake();
    }

    Status GetStatus() {
        std::scoped_lock lock(g_mutex);
        return g_status;
    }

    Report GetReport() {
        std::scoped_lock lock(g_mutex);
        return g_report;
    }

    void DeleteAll() {
        {
            std::scoped_lock lock(g_mutex);
            g_delete = true;
        }
        StartThread();
        g_wake.notify_all();
    }

    namespace {

        SDL_Texture *CachedTextureFor(u64 application_id, Kind kind, SDL_Texture *(*load)(const std::string &path), Kind file_kind) {
            if (g_textures_stale.exchange(false)) {
                g_textures.clear();
            }
            const u32 generation = g_generation;
            for (auto it = g_textures.begin(); it != g_textures.end(); ++it) {
                if (it->application_id == application_id && it->kind == kind) {
                    if (it->texture == nullptr && it->generation != generation) {
                        g_textures.erase(it);
                        break;
                    }
                    g_textures.splice(g_textures.begin(), g_textures, it);
                    return it->texture != nullptr ? it->texture->Get() : nullptr;
                }
            }
            pu::sdl2::TextureHandle::Ref handle;
            if (const std::string path = ArtPath(application_id, file_kind); sd::Exists(path)) {
                if (SDL_Texture *texture = load(path); texture != nullptr) {
                    handle = pu::sdl2::TextureHandle::New(texture);
                }
            }
            g_textures.push_front({ application_id, kind, handle, generation });
            while (g_textures.size() > MaxTextures) {
                g_textures.pop_back();
            }
            return handle != nullptr ? handle->Get() : nullptr;
        }

        SDL_Texture *LoadWhole(const std::string &path) {
            return pu::ui::render::LoadImageFromFile(path);
        }

    }

    SDL_Texture *GetTexture(u64 application_id, Kind kind) {
        return CachedTextureFor(application_id, kind, LoadWhole, kind);
    }

    SDL_Texture *GetSpineTexture(u64 application_id) {
        return CachedTextureFor(application_id, SpineKind, LoadSpine, Kind::Spread);
    }

    SDL_Rect SpreadBack(SDL_Texture *spread) {
        const SDL_Rect size = TextureSize(spread);
        return { 0, 0, std::min(size.w / 2, static_cast<int>(size.h * CoverAspect)), size.h };
    }

    SDL_Rect SpreadFront(SDL_Texture *spread) {
        const SDL_Rect size = TextureSize(spread);
        const int width = std::min(size.w / 2, static_cast<int>(size.h * CoverAspect));
        return { size.w - width, 0, width, size.h };
    }

    void Shutdown() {
        {
            std::scoped_lock lock(g_mutex);
            g_stop = true;
        }
        g_wake.notify_all();
        if (g_thread_started) {
            threadWaitForExit(&g_thread);
            threadClose(&g_thread);
            g_thread_started = false;
        }
        g_textures.clear();
        if (g_network_ready) {
            curl_global_cleanup();
            socketExit();
            g_network_ready = false;
        }
    }

}
