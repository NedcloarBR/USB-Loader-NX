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
#include "usbl_hidden_games.hpp"
#include "../usbl_log.hpp"

namespace ams::mitm::usbl {

    namespace {

        constexpr s32 MaxHiddenGames = 64;
        /* "0100000000000000\n" */
        constexpr size_t LineLength = 2 * sizeof(u64) + 1;

        constinit os::SdkMutex g_hidden_lock;
        constinit ncm::ApplicationId g_hidden[MaxHiddenGames] = {};
        constinit s32 g_hidden_count = 0;

        void FormatHiddenGamesPath(char *dst, size_t dst_size) {
            util::SNPrintf(dst, dst_size, "%s:/atmosphere/config/usbloadernx/hidden_games.txt", ams::fs::impl::SdCardFileSystemMountName);
        }

        ncm::ApplicationId *FindLocked(ncm::ApplicationId application_id) {
            ncm::ApplicationId *end = g_hidden + g_hidden_count;
            ncm::ApplicationId *it = std::find(g_hidden, end, application_id);
            return it != end ? it : nullptr;
        }

        Result SaveLocked() {
            char text[MaxHiddenGames * LineLength + 1];
            size_t len = 0;
            for (s32 i = 0; i < g_hidden_count; ++i) {
                len += util::SNPrintf(text + len, sizeof(text) - len, "%016" PRIx64 "\n", g_hidden[i].value);
            }

            char path[ams::fs::EntryNameLengthMax + 1];
            FormatHiddenGamesPath(path, sizeof(path));
            R_DISCARD(ams::fs::DeleteFile(path));
            R_SUCCEED_IF(len == 0);

            R_TRY(ams::fs::CreateFile(path, len));
            ams::fs::FileHandle file;
            R_TRY(ams::fs::OpenFile(std::addressof(file), path, ams::fs::OpenMode_Write));
            ON_SCOPE_EXIT { ams::fs::CloseFile(file); };
            R_RETURN(ams::fs::WriteFile(file, 0, text, len, ams::fs::WriteOption::Flush));
        }

    }

    void LoadHiddenGames() {
        std::scoped_lock lk(g_hidden_lock);
        g_hidden_count = 0;

        char path[ams::fs::EntryNameLengthMax + 1];
        FormatHiddenGamesPath(path, sizeof(path));

        ams::fs::FileHandle file;
        if (R_FAILED(ams::fs::OpenFile(std::addressof(file), path, ams::fs::OpenMode_Read))) {
            return;
        }
        ON_SCOPE_EXIT { ams::fs::CloseFile(file); };

        char text[MaxHiddenGames * LineLength * 2 + 1] = {};
        size_t read = 0;
        if (R_FAILED(ams::fs::ReadFile(std::addressof(read), file, 0, text, sizeof(text) - 1))) {
            return;
        }

        for (char *line = text; *line != '\x00' && g_hidden_count < MaxHiddenGames; ) {
            char *end = nullptr;
            const u64 id = std::strtoull(line, std::addressof(end), 16);
            if (end != line && id != 0) {
                g_hidden[g_hidden_count++] = { id };
            }
            line = std::strchr(end != line ? end : line, '\n');
            if (line == nullptr) {
                break;
            }
            ++line;
        }
        Log("hidden games: %d", g_hidden_count);
    }

    bool IsHiddenGame(ncm::ApplicationId application_id) {
        std::scoped_lock lk(g_hidden_lock);
        return FindLocked(application_id) != nullptr;
    }

    Result SetHiddenGame(ncm::ApplicationId application_id, bool hidden) {
        std::scoped_lock lk(g_hidden_lock);

        ncm::ApplicationId *existing = FindLocked(application_id);
        if (hidden) {
            R_SUCCEED_IF(existing != nullptr);
            R_UNLESS(g_hidden_count < MaxHiddenGames, ams::fs::ResultInvalidSize());
            g_hidden[g_hidden_count++] = application_id;
        } else {
            R_SUCCEED_IF(existing == nullptr);
            *existing = g_hidden[--g_hidden_count];
        }
        R_RETURN(SaveLocked());
    }

}
