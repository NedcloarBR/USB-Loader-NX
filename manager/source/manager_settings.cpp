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
#include <cstdlib>
#include <sd_files.hpp>

namespace settings {

    namespace {

        constexpr const char Section[] = "manager";

        /* Other sections, and the keys of [manager] this version does not use, are written back as they were. */
        sd::IniFile g_file;
        bool g_from_newer_version = false;

        std::string FilePath() {
            return sd::Path(usbl::paths::ManagerFile);
        }

        sd::IniFile::Entries &Values() {
            return g_file.Section(Section);
        }

        const std::string *Find(const char *key) {
            for (const auto &[name, value] : Values()) {
                if (name == key) {
                    return &value;
                }
            }
            return nullptr;
        }

    }

    void Load() {
        sd::ReadIni(&g_file, FilePath());
        g_from_newer_version = g_file.version > usbl::formats::Manager;
    }

    bool IsFromNewerVersion() {
        return g_from_newer_version;
    }

    s32 GetInt(const char *key, s32 default_value) {
        const std::string *found = Find(key);
        if (found == nullptr || found->empty()) {
            return default_value;
        }
        char *end = nullptr;
        const long value = std::strtol(found->c_str(), &end, 10);
        return *end == '\0' ? static_cast<s32>(value) : default_value;
    }

    std::string GetString(const char *key, const std::string &default_value) {
        const std::string *found = Find(key);
        return found != nullptr ? *found : default_value;
    }

    bool SetInt(const char *key, s32 value) {
        return SetString(key, std::to_string(value));
    }

    bool SetString(const char *key, const std::string &value) {
        auto &values = Values();
        bool replaced = false;
        for (auto &[name, text] : values) {
            if (name == key) {
                text = value;
                replaced = true;
            }
        }
        if (!replaced) {
            values.emplace_back(key, value);
        }

        /* A newer Manager's file is left as it is: the change holds until the Manager closes. */
        if (g_from_newer_version) {
            return true;
        }
        return sd::WriteIni(g_file, usbl::formats::Manager, FilePath());
    }

}
