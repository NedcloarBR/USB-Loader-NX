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
#include <gametdb.hpp>
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <zlib.h>
#include <sd_files.hpp>

namespace gametdb {

    namespace {

        constexpr char IndexPath[] = "romfs:/gametdb/index.bin";
        constexpr u32 IndexVersion = 2;
        /* id, regions, developer, publisher, year, genres, players, modes, ratings, then the titles. */
        constexpr size_t TitlesField = 9;
        constexpr char OverrideSection[] = "gametdb";

        /* Latin letters with marks and what they fold to (the same table as build_index.py). */
        constexpr std::pair<const char *, char> Fold[] = {
            { "àáâãäåāăąÀÁÂÃÄÅĀĂĄ", 'a' }, { "çćĉċčÇĆĈĊČ", 'c' }, { "ďđĎĐ", 'd' }, { "èéêëēĕėęěÈÉÊËĒĔĖĘĚ", 'e' },
            { "ĝğġģĜĞĠĢ", 'g' }, { "ĥħĤĦ", 'h' }, { "ìíîïĩīĭįıÌÍÎÏĨĪĬĮ", 'i' }, { "ĵĴ", 'j' }, { "ķĶ", 'k' },
            { "ĺļľŀłĹĻĽĿŁ", 'l' }, { "ñńņňŉÑŃŅŇ", 'n' }, { "òóôõöøōŏőÒÓÔÕÖØŌŎŐ", 'o' }, { "ŕŗřŔŖŘ", 'r' },
            { "śŝşšßŚŜŞŠ", 's' }, { "ţťŧŢŤŦ", 't' }, { "ùúûüũūŭůűųÙÚÛÜŨŪŬŮŰŲ", 'u' }, { "ŵŴ", 'w' },
            { "ýÿŷÝŸŶ", 'y' }, { "źżžŹŻŽ", 'z' },
        };

        /* Punctuation and symbols outside ASCII, read as spaces (the same ranges as build_index.py). */
        constexpr std::pair<u32, u32> SpaceRanges[] = {
            { 0x00A0, 0x00BF }, { 0x00D7, 0x00D7 }, { 0x00F7, 0x00F7 }, { 0x2000, 0x206F }, { 0x2100, 0x214F },
            { 0x2190, 0x2BFF }, { 0x3000, 0x303F }, { 0x30FB, 0x30FB }, { 0xFF01, 0xFF0F }, { 0xFF1A, 0xFF20 },
            { 0xFF3B, 0xFF40 }, { 0xFF5B, 0xFF65 },
        };

        /* One UTF-8 code point from `text` at `*pos`, moving past it; a broken byte reads as itself. */
        u32 NextCodePoint(std::string_view text, size_t *pos, size_t *length) {
            const u8 lead = static_cast<u8>(text[*pos]);
            size_t count = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1;
            if (*pos + count > text.size()) {
                count = 1;
            }
            u32 code = count == 1 ? lead : lead & (0x7F >> count);
            for (size_t i = 1; i < count; ++i) {
                code = (code << 6) | (static_cast<u8>(text[*pos + i]) & 0x3F);
            }
            *length = count;
            *pos += count;
            return code;
        }

        char FoldedLetter(std::string_view character) {
            for (const auto &[marked, plain] : Fold) {
                const std::string_view letters(marked);
                for (size_t pos = 0; pos < letters.size();) {
                    size_t length = 0;
                    const size_t start = pos;
                    NextCodePoint(letters, &pos, &length);
                    if (letters.substr(start, length) == character) {
                        return plain;
                    }
                }
            }
            return 0;
        }

        struct Index {
            std::vector<Entry> entries;
            std::unordered_map<std::string, std::vector<u32>> by_name;
            bool loaded = false;
        };

        std::mutex g_mutex;
        Index g_index;
        /* Read once: data/gametdb.ini and the console's region. */
        std::vector<std::pair<std::string, std::string>> g_overrides;
        std::string g_console_region;

        std::vector<std::string> Split(std::string_view text) {
            std::vector<std::string> parts;
            for (size_t start = 0; start < text.size();) {
                size_t end = text.find(',', start);
                if (end == std::string_view::npos) {
                    end = text.size();
                }
                if (end > start) {
                    parts.emplace_back(text.substr(start, end - start));
                }
                start = end + 1;
            }
            return parts;
        }

        std::vector<u8> ReadAll(const char *path) {
            std::vector<u8> data;
            FILE *file = std::fopen(path, "rb");
            if (file == nullptr) {
                return data;
            }
            std::fseek(file, 0, SEEK_END);
            const long size = std::ftell(file);
            std::rewind(file);
            if (size > 0) {
                data.resize(static_cast<size_t>(size));
                if (std::fread(data.data(), 1, data.size(), file) != data.size()) {
                    data.clear();
                }
            }
            std::fclose(file);
            return data;
        }

        void LoadIndex() {
            g_index.loaded = true;
            const std::vector<u8> file = ReadAll(IndexPath);
            u32 version = 0, text_size = 0;
            if (file.size() < 12 || std::memcmp(file.data(), "UTDB", 4) != 0) {
                return;
            }
            std::memcpy(&version, file.data() + 4, 4);
            std::memcpy(&text_size, file.data() + 8, 4);
            if (version != IndexVersion || text_size == 0 || text_size > 16 * 1024 * 1024) {
                return;
            }
            std::string text(text_size, '\0');
            uLongf out_size = text_size;
            if (uncompress(reinterpret_cast<Bytef *>(text.data()), &out_size, file.data() + 12, file.size() - 12) != Z_OK || out_size != text_size) {
                return;
            }

            /* "id \t regions \t title \t title ... \n" */
            for (size_t line_start = 0; line_start < text.size();) {
                size_t line_end = text.find('\n', line_start);
                if (line_end == std::string::npos) {
                    line_end = text.size();
                }
                const std::string_view line(text.data() + line_start, line_end - line_start);
                line_start = line_end + 1;

                size_t field_start = 0;
                std::vector<std::string_view> fields;
                while (field_start <= line.size()) {
                    size_t field_end = line.find('\t', field_start);
                    if (field_end == std::string_view::npos) {
                        field_end = line.size();
                    }
                    fields.push_back(line.substr(field_start, field_end - field_start));
                    field_start = field_end + 1;
                }
                if (fields.size() <= TitlesField) {
                    continue;
                }
                const u32 index = static_cast<u32>(g_index.entries.size());
                Entry entry;
                entry.id = fields[0];
                entry.regions = fields[1];
                entry.developer = fields[2];
                entry.publisher = fields[3];
                entry.year = fields[4];
                entry.genres = Split(fields[5]);
                entry.players = fields[6];
                entry.tv = fields[7].find('t') != std::string_view::npos;
                entry.handheld = fields[7].find('h') != std::string_view::npos;
                entry.tabletop = fields[7].find('m') != std::string_view::npos;
                for (const auto &rating : Split(fields[8])) {
                    if (const size_t equals = rating.find('='); equals != std::string::npos) {
                        entry.ratings.push_back({ rating.substr(0, equals), rating.substr(equals + 1) });
                    }
                }
                g_index.entries.push_back(std::move(entry));
                for (size_t i = TitlesField; i < fields.size(); ++i) {
                    if (!fields[i].empty()) {
                        g_index.by_name[std::string(fields[i])].push_back(index);
                    }
                }
            }
        }

        /* The console's region, as GameTDB names regions. */
        const char *ReadConsoleRegion() {
            SetRegion region = SetRegion_USA;
            if (R_SUCCEEDED(setInitialize())) {
                setGetRegionCode(&region);
                setExit();
            }
            switch (region) {
                case SetRegion_JPN: return "JPN";
                case SetRegion_EUR: return "EUR";
                case SetRegion_AUS: return "EUR";
                case SetRegion_HTK: return "TWN";
                case SetRegion_CHN: return "CHN";
                default:            return "USA";
            }
        }

        const char *ConsoleRegion() {
            if (g_console_region.empty()) {
                g_console_region = ReadConsoleRegion();
            }
            return g_console_region.c_str();
        }

        bool HasRegion(const std::string &regions, std::string_view region) {
            for (size_t start = 0; start <= regions.size();) {
                size_t end = regions.find(',', start);
                if (end == std::string::npos) {
                    end = regions.size();
                }
                if (std::string_view(regions).substr(start, end - start) == region) {
                    return true;
                }
                start = end + 1;
            }
            return false;
        }

        /* Higher is better: the console's region, then worldwide, then the US release. */
        int RegionScore(const Entry &entry, const char *console_region) {
            if (HasRegion(entry.regions, console_region)) {
                return 3;
            }
            if (HasRegion(entry.regions, "ALL")) {
                return 2;
            }
            return HasRegion(entry.regions, "USA") ? 1 : 0;
        }

        std::string TitleIdKey(u64 application_id) {
            char key[17];
            std::snprintf(key, sizeof(key), "%016" PRIX64, application_id);
            return key;
        }

        void LoadOverrides() {
            sd::IniFile file;
            if (sd::ReadIni(&file, sd::Path(usbl::paths::GameTdbIds))) {
                if (const auto *section = file.FindSection(OverrideSection); section != nullptr) {
                    g_overrides = *section;
                }
            }
        }

        bool FindOverride(u64 application_id, Entry *out) {
            const std::string key = TitleIdKey(application_id);
            for (const auto &[title_id, id] : g_overrides) {
                if (strcasecmp(title_id.c_str(), key.c_str()) == 0 && !id.empty()) {
                    *out = { .id = id };
                    for (const auto &entry : g_index.entries) {
                        if (entry.id == id) {
                            *out = entry;
                            break;
                        }
                    }
                    return true;
                }
            }
            return false;
        }

    }

    std::string Normalize(const std::string &name) {
        std::string out;
        bool pending_space = false;
        const auto put = [&](std::string_view text) {
            if (pending_space && !out.empty()) {
                out.push_back(' ');
            }
            pending_space = false;
            out.append(text);
        };
        for (size_t pos = 0; pos < name.size();) {
            const size_t start = pos;
            size_t length = 0;
            const u32 code = NextCodePoint(name, &pos, &length);
            const std::string_view character(name.data() + start, length);
            if (code < 0x80) {
                const char c = static_cast<char>(code);
                if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')) {
                    put(std::string_view(&c, 1));
                } else if (c >= 'A' && c <= 'Z') {
                    const char lower = static_cast<char>(c - 'A' + 'a');
                    put(std::string_view(&lower, 1));
                } else {
                    pending_space = true;
                }
                continue;
            }
            if (const char folded = FoldedLetter(character); folded != 0) {
                put(std::string_view(&folded, 1));
                continue;
            }
            bool is_space = false;
            for (const auto &[low, high] : SpaceRanges) {
                is_space = is_space || (code >= low && code <= high);
            }
            if (is_space) {
                pending_space = true;
            } else {
                put(character);
            }
        }
        return out;
    }

    bool Find(u64 application_id, const std::vector<std::string> &names, Entry *out) {
        std::scoped_lock lock(g_mutex);
        if (!g_index.loaded) {
            LoadIndex();
            LoadOverrides();
        }
        if (FindOverride(application_id, out)) {
            return true;
        }
        const char *console_region = ConsoleRegion();
        for (const auto &name : names) {
            const auto found = g_index.by_name.find(Normalize(name));
            if (found == g_index.by_name.end()) {
                continue;
            }
            const Entry *best = nullptr;
            int best_score = -1;
            for (const u32 index : found->second) {
                const Entry &entry = g_index.entries[index];
                if (const int score = RegionScore(entry, console_region); score > best_score) {
                    best = &entry;
                    best_score = score;
                }
            }
            if (best != nullptr) {
                *out = *best;
                return true;
            }
        }
        return false;
    }

    std::vector<std::string> ArtRegions(const Entry &entry) {
        std::vector<std::string> regions;
        const auto add = [&regions](const char *region) {
            if (std::find(regions.begin(), regions.end(), region) == regions.end()) {
                regions.push_back(region);
            }
        };
        if (HasRegion(entry.regions, "JPN") && !HasRegion(entry.regions, "USA") && !HasRegion(entry.regions, "EUR") && !HasRegion(entry.regions, "ALL")) {
            add("JA");
        }
        if (HasRegion(entry.regions, "KOR") && !HasRegion(entry.regions, "USA") && !HasRegion(entry.regions, "ALL")) {
            add("KO");
        }
        const std::string_view console = ConsoleRegion();
        if (console == "EUR") {
            add("EN");
        } else if (console == "JPN") {
            add("JA");
        }
        add("US");
        add("EN");
        return regions;
    }

}
