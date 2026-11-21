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
#include "usbl_hidden_games.hpp"
#include "../usbl_log.hpp"
#include "../usbl_results.hpp"
#include "../usbl_growable_array.hpp"

namespace ams::mitm::usbl {

    namespace {

        constexpr char HiddenSection[] = "hidden";
        /* "0100000000000000 = 1\n" */
        constexpr size_t LineLength = 2 * sizeof(u64) + 5;

        constinit os::SdkMutex g_hidden_lock;
        constinit GrowableArray<ncm::ApplicationId> g_hidden;
        constinit int g_file_version = ::usbl::formats::HiddenGames;

        void FormatHiddenGamesPath(char *dst, size_t dst_size) {
            util::SNPrintf(dst, dst_size, "%s:%s", ams::fs::impl::SdCardFileSystemMountName, ::usbl::paths::HiddenGames);
        }

        ncm::ApplicationId *FindLocked(ncm::ApplicationId application_id) {
            ncm::ApplicationId *it = std::find(g_hidden.begin(), g_hidden.end(), application_id);
            return it != g_hidden.end() ? it : nullptr;
        }

        void AddLocked(u64 id) {
            if (id != 0 && FindLocked({ id }) == nullptr) {
                g_hidden.PushBack({ id });
            }
        }

        int HiddenGamesIniHandler(void *, const char *section, const char *name, const char *value) {
            if (std::strcmp(section, ::usbl::formats::FormatSection) == 0) {
                if (std::strcmp(name, ::usbl::formats::VersionKey) == 0) {
                    g_file_version = std::max(1, static_cast<int>(std::strtol(value, nullptr, 10)));
                }
            } else if (std::strcmp(section, HiddenSection) == 0 && std::strcmp(value, "1") == 0) {
                AddLocked(std::strtoull(name, nullptr, 16));
            }
            return 1;
        }

        Result SaveLocked() {
            TemporaryArray<char> text(0x40 + g_hidden.size() * LineLength);
            R_UNLESS(text.Size() > 0, fs::ResultAllocationMemoryFailedNew());
            size_t len = util::SNPrintf(text.Get(), text.Size(), "[%s]\n%s = %d\n\n[%s]\n", ::usbl::formats::FormatSection, ::usbl::formats::VersionKey, ::usbl::formats::HiddenGames, HiddenSection);
            for (const auto &id : g_hidden) {
                len += util::SNPrintf(text.Get() + len, text.Size() - len, "%016" PRIx64 " = 1\n", id.value);
            }
            R_UNLESS(len < text.Size(), ams::fs::ResultInvalidSize());

            char directory[ams::fs::EntryNameLengthMax + 1];
            for (const char *dir : { ::usbl::paths::Root, ::usbl::paths::DataDir }) {
                util::SNPrintf(directory, sizeof(directory), "%s:%s", ams::fs::impl::SdCardFileSystemMountName, dir);
                R_DISCARD(ams::fs::CreateDirectory(directory));
            }

            char path[ams::fs::EntryNameLengthMax + 1];
            FormatHiddenGamesPath(path, sizeof(path));
            R_DISCARD(ams::fs::DeleteFile(path));

            R_TRY(ams::fs::CreateFile(path, len));
            ams::fs::FileHandle file;
            R_TRY(ams::fs::OpenFile(std::addressof(file), path, ams::fs::OpenMode_Write));
            ON_SCOPE_EXIT { ams::fs::CloseFile(file); };
            R_RETURN(ams::fs::WriteFile(file, 0, text.Get(), len, ams::fs::WriteOption::Flush));
        }

    }

    void LoadHiddenGames() {
        std::scoped_lock lk(g_hidden_lock);
        g_hidden.Clear();
        g_file_version = ::usbl::formats::HiddenGames;

        char path[ams::fs::EntryNameLengthMax + 1];
        FormatHiddenGamesPath(path, sizeof(path));

        ams::fs::FileHandle file;
        if (R_FAILED(ams::fs::OpenFile(std::addressof(file), path, ams::fs::OpenMode_Read))) {
            return;
        }
        ON_SCOPE_EXIT { ams::fs::CloseFile(file); };

        util::ini::ParseFile(file, nullptr, HiddenGamesIniHandler);
        Log("hidden games: %zu", g_hidden.size());
        if (g_file_version > ::usbl::formats::HiddenGames) {
            Log("hidden games: hidden_games.ini is from a newer version (format %d), read only", g_file_version);
        }
    }

    bool IsHiddenGamesFromNewerVersion() {
        std::scoped_lock lk(g_hidden_lock);
        return g_file_version > ::usbl::formats::HiddenGames;
    }

    bool IsHiddenGame(ncm::ApplicationId application_id) {
        std::scoped_lock lk(g_hidden_lock);
        return FindLocked(application_id) != nullptr;
    }

    Result SetHiddenGame(ncm::ApplicationId application_id, bool hidden) {
        std::scoped_lock lk(g_hidden_lock);
        R_UNLESS(g_file_version <= ::usbl::formats::HiddenGames, usbl::ResultNewerFormat());

        ncm::ApplicationId *existing = FindLocked(application_id);
        if (hidden) {
            R_SUCCEED_IF(existing != nullptr);
            R_UNLESS(g_hidden.PushBack(application_id), fs::ResultAllocationMemoryFailedNew());
        } else {
            R_SUCCEED_IF(existing == nullptr);
            g_hidden.RemoveIf([application_id](const ncm::ApplicationId &id) { return id == application_id; });
        }
        R_RETURN(SaveLocked());
    }

    void ConvertLegacyHiddenGames(const char *legacy_text, size_t size) {
        std::scoped_lock lk(g_hidden_lock);

        /* The old file: one application id in hex per line. */
        for (size_t pos = 0; pos < size; ) {
            char *end = nullptr;
            const u64 id = std::strtoull(legacy_text + pos, std::addressof(end), 16);
            AddLocked(id);
            const char *newline = static_cast<const char *>(std::memchr(legacy_text + pos, '\n', size - pos));
            if (newline == nullptr) {
                break;
            }
            pos = static_cast<size_t>(newline - legacy_text) + 1;
        }

        if (const Result result = SaveLocked(); R_FAILED(result)) {
            Log("hidden games: conversion not saved 2%03d-%04d", result.GetModule(), result.GetDescription());
        }
    }

}
