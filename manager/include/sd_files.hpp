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
#pragma once
#include <string>
#include <utility>
#include <vector>
#include <usbl/paths.hpp>

/* The Manager's files on the SD card (usbl/paths.hpp) and the small INI format they share with the module's. */
namespace sd {

    /* "sdmc:" + a path of usbl::paths. */
    std::string Path(const char *path);

    /* Creates each missing folder along `path` ("sdmc:/a/b/c"). */
    void MakeDirectories(const std::string &path);

    bool Exists(const std::string &path);

    /* "[section]" blocks of "key = value" lines, in file order. The "[format]" section is read into `version`
     * (1 without it) and written by Write, so it never shows up in `sections`. */
    struct IniFile {
        using Entries = std::vector<std::pair<std::string, std::string>>;

        int version = 1;
        std::vector<std::pair<std::string, Entries>> sections;

        /* The section, created at the end when missing. */
        Entries &Section(const std::string &name);
        const Entries *FindSection(const std::string &name) const;
    };

    /* False when the file cannot be opened; `out` is then empty. */
    bool ReadIni(IniFile *out, const std::string &path);
    /* Writes `[format] version = <version>` and then every section; creates the file's folder. */
    bool WriteIni(const IniFile &file, int version, const std::string &path);

}
