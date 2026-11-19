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
#include <manager_settings.hpp>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sys/stat.h>

namespace settings {

    namespace {

        constexpr const char FilePath[] = "sdmc:/config/usbloadernx-manager/manager.ini";

        std::map<std::string, std::string> g_values;

        std::string Trim(const std::string &text) {
            const size_t first = text.find_first_not_of(" \t\r\n");
            const size_t last = text.find_last_not_of(" \t\r\n");
            return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
        }

    }

    void Load() {
        g_values.clear();
        FILE *file = std::fopen(FilePath, "r");
        if (file == nullptr) {
            return;
        }
        char line[256];
        while (std::fgets(line, sizeof(line), file) != nullptr) {
            const std::string text = line;
            const size_t equals = text.find('=');
            if (equals == std::string::npos || text[0] == ';' || text[0] == '[') {
                continue;
            }
            g_values[Trim(text.substr(0, equals))] = Trim(text.substr(equals + 1));
        }
        std::fclose(file);
    }

    s32 GetInt(const char *key, s32 default_value) {
        const auto found = g_values.find(key);
        if (found == g_values.end() || found->second.empty()) {
            return default_value;
        }
        char *end = nullptr;
        const long value = std::strtol(found->second.c_str(), &end, 10);
        return *end == '\0' ? static_cast<s32>(value) : default_value;
    }

    std::string GetString(const char *key, const std::string &default_value) {
        const auto found = g_values.find(key);
        return found != g_values.end() ? found->second : default_value;
    }

    bool SetInt(const char *key, s32 value) {
        return SetString(key, std::to_string(value));
    }

    bool SetString(const char *key, const std::string &value) {
        g_values[key] = value;

        mkdir(Directory, 0777);
        FILE *file = std::fopen(FilePath, "w");
        if (file == nullptr) {
            return false;
        }
        std::fputs("[manager]\n", file);
        for (const auto &[name, text] : g_values) {
            std::fprintf(file, "%s = %s\n", name.c_str(), text.c_str());
        }
        std::fclose(file);
        return true;
    }

}
