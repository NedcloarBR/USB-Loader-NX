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
#include <game_library.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <SDL2/SDL_image.h>
#include <ui/theme.hpp>
#include <sd_files.hpp>

namespace library {

    namespace {

        /* Decoded on the CPU so the corners can be rounded once, with an antialiased edge, instead of on every frame. */
        pu::sdl2::TextureHandle::Ref LoadRoundedIcon(const void *jpeg, size_t size) {
            SDL_Surface *decoded = IMG_Load_RW(SDL_RWFromConstMem(jpeg, static_cast<int>(size)), 1);
            if (decoded == nullptr) {
                return nullptr;
            }
            SDL_Surface *surface = SDL_ConvertSurfaceFormat(decoded, SDL_PIXELFORMAT_RGBA32, 0);
            SDL_FreeSurface(decoded);
            if (surface == nullptr) {
                return nullptr;
            }

            const int width = surface->w, height = surface->h;
            const double radius = std::min(width, height) * ui::IconCornerRatio;
            SDL_LockSurface(surface);
            for (int y = 0; y < height; ++y) {
                u8 *row = static_cast<u8 *>(surface->pixels) + y * surface->pitch;
                for (int x = 0; x < width; ++x) {
                    /* Only the corner squares: distance to the corner circle's center gives the pixel's coverage. */
                    const double cx = x + 0.5 < radius ? radius : (x + 0.5 > width - radius ? width - radius : x + 0.5);
                    const double cy = y + 0.5 < radius ? radius : (y + 0.5 > height - radius ? height - radius : y + 0.5);
                    const double distance = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
                    if (distance > radius - 0.5) {
                        const double coverage = std::clamp(radius - distance + 0.5, 0.0, 1.0);
                        row[x * 4 + 3] = static_cast<u8>(row[x * 4 + 3] * coverage);
                    }
                }
            }
            SDL_UnlockSurface(surface);

            SDL_Texture *texture = SDL_CreateTextureFromSurface(pu::ui::render::GetMainRenderer(), surface);
            SDL_FreeSurface(surface);
            if (texture == nullptr) {
                return nullptr;
            }
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            return pu::sdl2::TextureHandle::New(texture);
        }

        constexpr const char FavoritesSection[] = "favorites";

        /* usbloadernx/data/favorites.ini: "<application id> = 1" under [favorites], in the order they were added. */
        std::vector<u64> ReadFavorites(sd::IniFile *out_file = nullptr) {
            sd::IniFile file;
            sd::ReadIni(&file, sd::Path(usbl::paths::Favorites));

            std::vector<u64> favorites;
            if (const auto *entries = file.FindSection(FavoritesSection); entries != nullptr) {
                for (const auto &[key, value] : *entries) {
                    if (const u64 application_id = std::strtoull(key.c_str(), nullptr, 16); application_id != 0 && value == "1") {
                        favorites.push_back(application_id);
                    }
                }
            }
            if (out_file != nullptr) {
                *out_file = std::move(file);
            }
            return favorites;
        }

        PlayStats QueryPlayStats(u64 application_id) {
            PdmPlayStatistics statistics = {};
            if (R_FAILED(pdmqryQueryPlayStatisticsByApplicationId(application_id, false, &statistics))) {
                return {};
            }
            return { .play_time_ns = statistics.playtime, .launches = statistics.total_launches, .last_played = statistics.total_launches > 0 ? statistics.last_timestamp_user : 0 };
        }

        std::string Lowercase(std::string text) {
            for (char &c : text) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            return text;
        }

        /* The games' own icons, kept for when their drive is unplugged. */
        std::string IconCachePath(u64 application_id) {
            char path[96];
            std::snprintf(path, sizeof(path), "sdmc:%s/%016" PRIX64 ".jpg", usbl::paths::IconCacheDir, application_id);
            return path;
        }

        std::vector<u8> ReadFile(const std::string &path) {
            std::vector<u8> data;
            if (FILE *file = std::fopen(path.c_str(), "rb"); file != nullptr) {
                std::fseek(file, 0, SEEK_END);
                const long size = std::ftell(file);
                std::fseek(file, 0, SEEK_SET);
                if (size > 0 && static_cast<size_t>(size) <= usbl::protocol::MaxIconSize) {
                    data.resize(static_cast<size_t>(size));
                    data.resize(std::fread(data.data(), 1, data.size(), file));
                }
                std::fclose(file);
            }
            return data;
        }

        /* Written only when it changed (a new update may bring a new icon), to spare the SD card. */
        void CacheIcon(const std::string &path, const std::vector<u8> &jpeg) {
            if (ReadFile(path) == jpeg) {
                return;
            }
            sd::MakeDirectories(sd::Path(usbl::paths::IconCacheDir));
            if (FILE *file = std::fopen(path.c_str(), "wb"); file != nullptr) {
                std::fwrite(jpeg.data(), 1, jpeg.size(), file);
                std::fclose(file);
            }
        }

        /* The icon without the badge HOME shows: from the drive when it is plugged in, else from the cache. */
        bool LoadPlainIcon(Game *game) {
            const u64 application_id = game->status.application_id;
            const std::string path = IconCachePath(application_id);
            std::vector<u8> jpeg;
            if (game->status.is_connected && R_SUCCEEDED(usbl::GetGameIcon(application_id, &jpeg)) && !jpeg.empty()) {
                CacheIcon(path, jpeg);
            } else {
                jpeg = ReadFile(path);
            }
            if (jpeg.empty()) {
                return false;
            }
            game->icon = LoadRoundedIcon(jpeg.data(), jpeg.size());
            return game->icon != nullptr;
        }

        void LoadControlData(Game *game, bool with_icon) {
            auto data = std::make_unique<NsApplicationControlData>();
            u64 size = 0;
            if (R_FAILED(nsGetApplicationControlData(NsApplicationControlSource_Storage, game->status.application_id, data.get(), sizeof(*data), &size)) || size < sizeof(data->nacp)) {
                return;
            }

            /* The version players know ("1.0.2"), from the update when one is registered. */
            game->display_version.assign(data->nacp.display_version, strnlen(data->nacp.display_version, sizeof(data->nacp.display_version)));

            NacpLanguageEntry *entry = nullptr;
            if (R_SUCCEEDED(nacpGetLanguageEntry(&data->nacp, &entry)) && entry != nullptr && entry->name[0] != '\x00') {
                game->name = entry->name;
            }

            const size_t icon_size = size - sizeof(data->nacp);
            if (with_icon && icon_size > 0) {
                game->icon = LoadRoundedIcon(data->icon, icon_size);
            }
        }

        std::string FormatApplicationId(u64 application_id) {
            char text[17];
            std::snprintf(text, sizeof(text), "%016" PRIX64, application_id);
            return text;
        }

        bool IsAutoRegisterEnabled() {
            usbl::ConfigValue value = {};
            return R_SUCCEEDED(usbl::GetConfigValue(&value, usbl::ConfigKey::AutoRegister)) && value.enabled;
        }

    }

    pu::sdl2::TextureHandle::Ref LoadRoundedImage(const char *path) {
        FILE *file = std::fopen(path, "rb");
        if (file == nullptr) {
            return {};
        }
        std::vector<u8> data;
        u8 chunk[0x1000];
        for (size_t read; (read = std::fread(chunk, 1, sizeof(chunk), file)) > 0;) {
            data.insert(data.end(), chunk, chunk + read);
        }
        std::fclose(file);
        return data.empty() ? pu::sdl2::TextureHandle::Ref() : LoadRoundedIcon(data.data(), data.size());
    }

    Result LoadGames(std::vector<Game> *out) {
        std::vector<usbl::GameStatus> statuses;
        if (const Result rc = usbl::ListGames(&statuses); R_FAILED(rc)) {
            return rc;
        }

        const std::vector<u64> favorites = ReadFavorites();
        const bool has_play_records = R_SUCCEEDED(pdmqryInitialize());
        const bool has_versions = R_SUCCEEDED(avmInitialize());

        out->clear();
        out->reserve(statuses.size());
        for (const auto &status : statuses) {
            Game game = { .status = status, .name = FormatApplicationId(status.application_id), .display_version = {}, .icon = {} };
            LoadControlData(&game, !LoadPlainIcon(&game));
            /* ns's copy can still be the old one right after the drive brings an update. */
            if (std::string version; status.is_connected && R_SUCCEEDED(usbl::GetGameDisplayVersion(status.application_id, &version)) && !version.empty()) {
                game.display_version = std::move(version);
            }
            if (has_play_records) {
                game.play = QueryPlayStats(status.application_id);
            }
            game.is_favorite = std::find(favorites.begin(), favorites.end(), status.application_id) != favorites.end();
            if (u32 required = 0; has_versions && R_SUCCEEDED(avmGetLaunchRequiredVersion(status.application_id, &required))) {
                game.update_missing = required > (status.has_update ? status.update_version : status.version);
            }
            out->push_back(std::move(game));
        }

        if (has_versions) {
            avmExit();
        }
        if (has_play_records) {
            pdmqryExit();
        }
        return 0;
    }

    bool IsUpdateMissing(u64 application_id) {
        std::vector<usbl::GameStatus> statuses;
        if (R_FAILED(usbl::ListGames(&statuses))) {
            return false;
        }
        const auto status = std::find_if(statuses.begin(), statuses.end(), [application_id](const usbl::GameStatus &s) { return s.application_id == application_id; });
        if (status == statuses.end() || R_FAILED(avmInitialize())) {
            return false;
        }
        u32 required = 0;
        const bool ok = R_SUCCEEDED(avmGetLaunchRequiredVersion(application_id, &required));
        avmExit();
        return ok && required > (status->has_update ? status->update_version : status->version);
    }

    bool SetFavorite(u64 application_id, bool favorite) {
        sd::IniFile file;
        std::vector<u64> favorites = ReadFavorites(&file);
        /* A newer version's list is left as it is. */
        if (file.version > usbl::formats::Favorites) {
            return false;
        }
        favorites.erase(std::remove(favorites.begin(), favorites.end(), application_id), favorites.end());
        if (favorite) {
            favorites.push_back(application_id);
        }
        return WriteFavorites(favorites);
    }

    bool WriteFavorites(const std::vector<u64> &favorites) {
        sd::IniFile file;
        auto &entries = file.Section(FavoritesSection);
        for (const u64 id : favorites) {
            char key[17];
            std::snprintf(key, sizeof(key), "%016" PRIx64, id);
            entries.emplace_back(key, "1");
        }
        return sd::WriteIni(file, usbl::formats::Favorites, sd::Path(usbl::paths::Favorites));
    }

    bool AreFavoritesFromNewerVersion() {
        sd::IniFile file;
        return sd::ReadIni(&file, sd::Path(usbl::paths::Favorites)) && file.version > usbl::formats::Favorites;
    }

    void SortGames(std::vector<Game> *games, SortMode mode) {
        std::stable_sort(games->begin(), games->end(), [](const Game &a, const Game &b) { return Lowercase(a.name) < Lowercase(b.name); });
        std::stable_sort(games->begin(), games->end(), [mode](const Game &a, const Game &b) {
            if (a.is_favorite != b.is_favorite) {
                return a.is_favorite;
            }
            switch (mode) {
                case SortMode::RecentlyPlayed: return a.play.last_played > b.play.last_played;
                case SortMode::MostPlayed:     return a.play.play_time_ns > b.play.play_time_ns;
                case SortMode::Size:           return a.status.size > b.status.size;
                default:                       return false;
            }
        });
    }

    void FilterGames(std::vector<Game> *games, const std::string &query) {
        if (query.empty()) {
            return;
        }
        const std::string needle = Lowercase(query);
        games->erase(std::remove_if(games->begin(), games->end(), [&](const Game &game) { return Lowercase(game.name).find(needle) == std::string::npos; }), games->end());
    }

    Result LaunchGame(const Game &game) {
        /* ns only launches games it has a record for. */
        if (game.status.is_hidden || !IsAutoRegisterEnabled()) {
            if (const Result rc = usbl::ShowGameForLaunch(game.status.application_id); R_FAILED(rc)) {
                return rc;
            }
        }
        return appletRequestLaunchApplication(game.status.application_id, nullptr);
    }

}
