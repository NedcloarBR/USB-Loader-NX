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
#include <sd_files.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

namespace sd {

    namespace {

        std::string Trim(const std::string &text) {
            const size_t first = text.find_first_not_of(" \t\r\n");
            const size_t last = text.find_last_not_of(" \t\r\n");
            return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
        }

    }

    std::string Path(const char *path) {
        return std::string("sdmc:") + path;
    }

    void MakeDirectories(const std::string &path) {
        /* Past "sdmc:/", each separator ends a folder to create. */
        for (size_t slash = path.find('/', path.find(":/") + 2); ; slash = path.find('/', slash + 1)) {
            mkdir(path.substr(0, slash).c_str(), 0777);
            if (slash == std::string::npos) {
                break;
            }
        }
    }

    bool Exists(const std::string &path) {
        struct stat info;
        return stat(path.c_str(), &info) == 0;
    }

    IniFile::Entries &IniFile::Section(const std::string &name) {
        for (auto &[section_name, entries] : this->sections) {
            if (section_name == name) {
                return entries;
            }
        }
        return this->sections.emplace_back(name, Entries{}).second;
    }

    const IniFile::Entries *IniFile::FindSection(const std::string &name) const {
        for (const auto &[section_name, entries] : this->sections) {
            if (section_name == name) {
                return &entries;
            }
        }
        return nullptr;
    }

    bool ReadIni(IniFile *out, const std::string &path) {
        *out = {};
        FILE *file = std::fopen(path.c_str(), "r");
        if (file == nullptr) {
            return false;
        }

        std::string section;
        char line[512];
        while (std::fgets(line, sizeof(line), file) != nullptr) {
            const std::string text = Trim(line);
            if (text.empty() || text[0] == ';' || text[0] == '#') {
                continue;
            }
            if (text[0] == '[') {
                section = Trim(text.substr(1, text.find(']') - 1));
                continue;
            }
            const size_t equals = text.find('=');
            if (equals == std::string::npos) {
                continue;
            }
            const std::string key = Trim(text.substr(0, equals)), value = Trim(text.substr(equals + 1));
            if (section == usbl::formats::FormatSection) {
                if (key == usbl::formats::VersionKey) {
                    out->version = std::max(1, static_cast<int>(std::strtol(value.c_str(), nullptr, 10)));
                }
                continue;
            }
            out->Section(section).emplace_back(key, value);
        }
        std::fclose(file);
        return true;
    }

    bool WriteIni(const IniFile &ini, int version, const std::string &path) {
        MakeDirectories(path.substr(0, path.rfind('/')));
        FILE *file = std::fopen(path.c_str(), "w");
        if (file == nullptr) {
            return false;
        }
        std::fprintf(file, "[%s]\n%s = %d\n", usbl::formats::FormatSection, usbl::formats::VersionKey, version);
        for (const auto &[name, entries] : ini.sections) {
            std::fprintf(file, "\n[%s]\n", name.c_str());
            for (const auto &[key, value] : entries) {
                std::fprintf(file, "%s = %s\n", key.c_str(), value.c_str());
            }
        }
        const bool ok = std::fflush(file) == 0;
        std::fclose(file);
        return ok;
    }

}
