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
#include "usbl_log.hpp"

namespace ams::mitm::usbl {

    namespace {

        constinit os::SdkMutex g_log_lock;
        constinit bool g_log_available = false;
        constinit char g_log_buffer[0x400];
        constinit char g_log_file_path[ams::fs::EntryNameLengthMax + 1];

    }

    void InitializeLog() {
        /* ams.mitm mounts the SD card during initialization; callers run after mitm::WaitInitialized. */
        const Result result = []() -> Result {
            char directory_path[ams::fs::EntryNameLengthMax + 1];
            for (const char *dir : { ::usbl::paths::Root, ::usbl::paths::LogsDir }) {
                util::SNPrintf(directory_path, sizeof(directory_path), "%s:%s", ams::fs::impl::SdCardFileSystemMountName, dir);
                R_DISCARD(ams::fs::CreateDirectory(directory_path));
            }
            util::SNPrintf(g_log_file_path, sizeof(g_log_file_path), "%s:%s", ams::fs::impl::SdCardFileSystemMountName, ::usbl::paths::LogFile);

            R_DISCARD(ams::fs::DeleteFile(g_log_file_path));
            R_RETURN(ams::fs::CreateFile(g_log_file_path, 0));
        }();

        /* Debug builds surface the failure as a crash report; release builds keep running without a log. */
        #if defined(AMS_BUILD_FOR_DEBUGGING)
        R_ABORT_UNLESS(result);
        #endif

        g_log_available = R_SUCCEEDED(result);
    }

    void Log(const char *fmt, ...) {
        std::scoped_lock lk(g_log_lock);

        if (!g_log_available) {
            return;
        }

        const s64 tick_ms = os::ConvertToTimeSpan(os::GetSystemTick()).GetMilliSeconds();
        int len = util::SNPrintf(g_log_buffer, sizeof(g_log_buffer), "[%10" PRId64 "] ", tick_ms);

        std::va_list vl;
        va_start(vl, fmt);
        len += util::VSNPrintf(g_log_buffer + len, sizeof(g_log_buffer) - len, fmt, vl);
        va_end(vl);

        len = std::min<int>(len, sizeof(g_log_buffer) - 2);
        g_log_buffer[len++] = '\n';

        ams::fs::FileHandle file;
        if (R_FAILED(ams::fs::OpenFile(std::addressof(file), g_log_file_path, ams::fs::OpenMode_Write | ams::fs::OpenMode_AllowAppend))) {
            return;
        }
        ON_SCOPE_EXIT { ams::fs::CloseFile(file); };

        s64 offset;
        if (R_SUCCEEDED(ams::fs::GetFileSize(std::addressof(offset), file))) {
            R_DISCARD(ams::fs::WriteFile(file, offset, g_log_buffer, len, ams::fs::WriteOption::Flush));
        }
    }

    bool IsDebugEnabled() {
        u8 enabled = 0;
        return ams::settings::fwdbg::GetSettingsItemValue(std::addressof(enabled), sizeof(enabled), "usbloadernx", "debug") == sizeof(enabled) && enabled != 0;
    }

}
