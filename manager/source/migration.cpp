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
#include <migration.hpp>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <string>
#include <vector>
#include <unistd.h>
#include <game_library.hpp>
#include <sd_files.hpp>

namespace migration {

    namespace {

        std::string Legacy(const char *child) {
            return sd::Path(usbl::paths::legacy::ManagerDir) + "/" + child;
        }

        std::vector<std::string> ListEntries(const std::string &directory) {
            std::vector<std::string> names;
            if (DIR *dir = opendir(directory.c_str()); dir != nullptr) {
                while (const dirent *entry = readdir(dir)) {
                    const std::string name = entry->d_name;
                    if (name != "." && name != "..") {
                        names.push_back(name);
                    }
                }
                closedir(dir);
            }
            return names;
        }

        /* When the new place already has it, the new one stays and the old one is kept as "<name>.old". */
        void MoveFile(const std::string &from, const std::string &to) {
            if (!sd::Exists(from)) {
                return;
            }
            std::rename(from.c_str(), sd::Exists(to) ? (to + ".old").c_str() : to.c_str());
        }

        /* A folder moves whole when the new one does not exist yet, else entry by entry, keeping what is already there. */
        void MoveFolder(const std::string &from, const std::string &to, bool drop_duplicates) {
            if (!sd::Exists(from)) {
                return;
            }
            if (!sd::Exists(to)) {
                sd::MakeDirectories(to.substr(0, to.rfind('/')));
                std::rename(from.c_str(), to.c_str());
                return;
            }
            for (const auto &name : ListEntries(from)) {
                const std::string source = from + "/" + name, destination = to + "/" + name;
                if (!sd::Exists(destination)) {
                    std::rename(source.c_str(), destination.c_str());
                } else if (drop_duplicates) {
                    std::remove(source.c_str());
                }
            }
            rmdir(from.c_str());
        }

        void ConvertFavorites() {
            const std::string path = Legacy("favorites.txt");
            FILE *file = std::fopen(path.c_str(), "r");
            if (file == nullptr) {
                return;
            }

            std::vector<u64> favorites;
            char line[32];
            while (std::fgets(line, sizeof(line), file) != nullptr) {
                if (const u64 application_id = std::strtoull(line, nullptr, 16); application_id != 0) {
                    favorites.push_back(application_id);
                }
            }
            std::fclose(file);

            if (library::WriteFavorites(favorites)) {
                std::remove(path.c_str());
            }
        }

    }

    void MoveLegacyFiles() {
        if (!sd::Exists(sd::Path(usbl::paths::legacy::ManagerDir))) {
            return;
        }
        sd::MakeDirectories(sd::Path(usbl::paths::Root));

        MoveFile(Legacy("manager.ini"), sd::Path(usbl::paths::ManagerFile));
        /* Only when there is no list in the new place yet: one written there already is the newer. */
        if (!sd::Exists(sd::Path(usbl::paths::Favorites))) {
            ConvertFavorites();
        }
        MoveFolder(Legacy("themes"), sd::Path(usbl::paths::ThemesDir), false);
        /* Icons are only a cache: a copy already in the new place wins. */
        MoveFolder(Legacy("icons"), sd::Path(usbl::paths::IconCacheDir), true);

        /* Only once it is empty: anything left (an unknown file, a theme with a name taken in the new place) keeps it. */
        rmdir(sd::Path(usbl::paths::legacy::ManagerDir).c_str());
    }

}
