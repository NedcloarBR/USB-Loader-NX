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
#include <stratosphere.hpp>
#include <usbl/paths.hpp>
#include "usbl_migration.hpp"
#include "usbl_log.hpp"
#include "content/usbl_hidden_games.hpp"

namespace ams::mitm::usbl {

    namespace {

        using Path = char[ams::fs::EntryNameLengthMax + 1];

        void MakeSdPath(Path &out, const char *path, const char *child = nullptr) {
            if (child != nullptr) {
                util::SNPrintf(out, sizeof(out), "%s:%s/%s", ams::fs::impl::SdCardFileSystemMountName, path, child);
            } else {
                util::SNPrintf(out, sizeof(out), "%s:%s", ams::fs::impl::SdCardFileSystemMountName, path);
            }
        }

        bool Exists(const char *path) {
            ams::fs::DirectoryEntryType type;
            return R_SUCCEEDED(ams::fs::GetEntryType(std::addressof(type), path));
        }

        /* When the new place already has the file (moved by hand, or a second copy), the new one stays and the old one
         * is kept next to it as "<name>.old": nothing is lost. */
        void MoveFile(const char *legacy_name, const char *destination) {
            Path from, to;
            MakeSdPath(from, ::usbl::paths::legacy::ModuleDir, legacy_name);
            MakeSdPath(to, destination);
            if (!Exists(from)) {
                return;
            }

            if (Exists(to)) {
                std::strncat(to, ".old", sizeof(to) - std::strlen(to) - 1);
                R_DISCARD(ams::fs::DeleteFile(to));
            }
            if (const Result result = ams::fs::RenameFile(from, to); R_FAILED(result)) {
                Log("migration: %s not moved 2%03d-%04d", legacy_name, result.GetModule(), result.GetDescription());
            } else {
                Log("migration: %s moved to %s", legacy_name, to + std::strlen(ams::fs::impl::SdCardFileSystemMountName) + 1);
            }
        }

        void ConvertHiddenGames() {
            Path from;
            MakeSdPath(from, ::usbl::paths::legacy::ModuleDir, "hidden_games.txt");

            ams::fs::FileHandle file;
            if (R_FAILED(ams::fs::OpenFile(std::addressof(file), from, ams::fs::OpenMode_Read))) {
                return;
            }

            char text[0x800] = {};
            size_t read = 0;
            const Result result = ams::fs::ReadFile(std::addressof(read), file, 0, text, sizeof(text) - 1);
            ams::fs::CloseFile(file);
            if (R_FAILED(result)) {
                Log("migration: hidden_games.txt unreadable, left in place");
                return;
            }

            ConvertLegacyHiddenGames(text, read);
            R_DISCARD(ams::fs::DeleteFile(from));
            Log("migration: hidden_games.txt converted to %s", ::usbl::paths::HiddenGames);
        }

    }

    void MigrateLegacyFiles() {
        Path legacy_dir;
        MakeSdPath(legacy_dir, ::usbl::paths::legacy::ModuleDir);

        /* The old log only held the boots before this one. */
        Path legacy_log;
        MakeSdPath(legacy_log, ::usbl::paths::legacy::ModuleLogFile);
        R_DISCARD(ams::fs::DeleteFile(legacy_log));

        if (!Exists(legacy_dir)) {
            return;
        }

        for (const char *dir : { ::usbl::paths::Root, ::usbl::paths::DataDir }) {
            Path path;
            MakeSdPath(path, dir);
            R_DISCARD(ams::fs::CreateDirectory(path));
        }

        MoveFile("config.ini", ::usbl::paths::ConfigFile);
        MoveFile("known_contents.bin", ::usbl::paths::KnownContents);
        ConvertHiddenGames();

        /* Only once it is empty; anything left (an unknown file, a folder that could not move) keeps it. */
        if (R_SUCCEEDED(ams::fs::DeleteDirectory(legacy_dir))) {
            Log("migration: %s removed", ::usbl::paths::legacy::ModuleDir);
        } else {
            Log("migration: %s not empty, left in place", ::usbl::paths::legacy::ModuleDir);
        }
    }

}
