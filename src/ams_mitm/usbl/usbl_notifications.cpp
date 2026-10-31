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
#include "usbl_notifications.hpp"
#include "usbl_config.hpp"
#include "i18n/usbl_i18n.hpp"
#include "usbl_log.hpp"

extern "C" {
    extern const u8 usbloadernx_connected_rgba[];
    extern const u8 usbloadernx_connected_rgba_end[];
    extern const u8 usbloadernx_removed_rgba[];
    extern const u8 usbloadernx_removed_rgba_end[];
    extern const u8 usbloadernx_unreadable_rgba[];
    extern const u8 usbloadernx_unreadable_rgba_end[];
}

namespace ams::mitm::usbl {

    namespace {

        /* Ultrahand reads the app name up to the first '-' (suppression flag) and the icon name up to the last '-'. */
        constexpr const char AppName[] = "usbloadernx";

        constexpr s32 DurationMs = 5000;
        constexpr s32 Priority   = 20;

        /* bin2s symbols: <file>_rgba and <file>_rgba_end. */
        struct NotificationIcon {
            const char *state;
            const u8 *begin;
            const u8 *end;
        };

        constexpr NotificationIcon ConnectedIcon  = { "connected",  usbloadernx_connected_rgba,  usbloadernx_connected_rgba_end };
        constexpr NotificationIcon RemovedIcon    = { "removed",    usbloadernx_removed_rgba,    usbloadernx_removed_rgba_end };
        constexpr NotificationIcon UnreadableIcon = { "unreadable", usbloadernx_unreadable_rgba, usbloadernx_unreadable_rgba_end };

        constinit bool g_enabled = false;
        constinit os::SdkMutex g_lock;
        constinit u32 g_sequence = 0;

        void FormatSdPath(char *dst, size_t dst_size, const char *path) {
            util::SNPrintf(dst, dst_size, "%s:%s", ams::fs::impl::SdCardFileSystemMountName, path);
        }

        bool IsUltrahandInstalled() {
            char path[ams::fs::EntryNameLengthMax + 1];
            FormatSdPath(path, sizeof(path), "/config/ultrahand");

            bool has_dir = false;
            return R_SUCCEEDED(ams::fs::HasDirectory(std::addressof(has_dir), path)) && has_dir;
        }

        Result WriteNewFile(const char *path, const void *data, size_t size) {
            R_TRY(ams::fs::CreateFile(path, size));

            ams::fs::FileHandle file;
            R_TRY(ams::fs::OpenFile(std::addressof(file), path, ams::fs::OpenMode_Write));
            ON_SCOPE_EXIT { ams::fs::CloseFile(file); };

            R_RETURN(ams::fs::WriteFile(file, 0, data, size, ams::fs::WriteOption::Flush));
        }

        /* Installed once; an icon the user replaced is left alone. */
        void InstallIcon(const NotificationIcon &icon) {
            char path[ams::fs::EntryNameLengthMax + 1];
            util::SNPrintf(path, sizeof(path), "%s:/config/ultrahand/assets/notifications/%s-%s.rgba", ams::fs::impl::SdCardFileSystemMountName, AppName, icon.state);

            bool has_file = false;
            if (R_SUCCEEDED(ams::fs::HasFile(std::addressof(has_file), path)) && has_file) {
                return;
            }

            if (const Result result = WriteNewFile(path, icon.begin, icon.end - icon.begin); R_FAILED(result)) {
                Log("notify: cannot install icon %s 2%03d-%04d", icon.state, result.GetModule(), result.GetDescription());
            }
        }

        size_t AppendJsonString(char *dst, size_t dst_size, size_t len, const char *src) {
            auto put = [&](char c) {
                if (len + 1 < dst_size) {
                    dst[len++] = c;
                }
            };

            put('"');
            for (; *src != '\x00'; ++src) {
                const unsigned char c = static_cast<unsigned char>(*src);
                if (c == '"' || c == '\\') {
                    put('\\');
                    put(static_cast<char>(c));
                } else if (c < 0x20) {
                    char escaped[8];
                    util::SNPrintf(escaped, sizeof(escaped), "\\u%04x", c);
                    for (const char *e = escaped; *e != '\x00'; ++e) {
                        put(*e);
                    }
                } else {
                    put(static_cast<char>(c));
                }
            }
            put('"');

            dst[len] = '\x00';
            return len;
        }

        void Post(const NotificationIcon &icon, const char *text) {
            if (!g_enabled || !IsConfigEnabled(ConfigKey::Notifications)) {
                return;
            }

            std::scoped_lock lk(g_lock);

            char title[128];
            i18n::Format(title, sizeof(title), "Notifications.Title");

            char json[1024];
            size_t len = util::SNPrintf(json, sizeof(json), "{\n  \"title\": ");
            len = AppendJsonString(json, sizeof(json), len, title);
            len += util::SNPrintf(json + len, sizeof(json) - len, ",\n  \"text\": ");
            len = AppendJsonString(json, sizeof(json), len, text);
            len += util::SNPrintf(json + len, sizeof(json) - len, ",\n  \"duration\": %d,\n  \"priority\": %d\n}\n", DurationMs, Priority);
            len = std::min(len, sizeof(json) - 1);

            char path[ams::fs::EntryNameLengthMax + 1];
            util::SNPrintf(path, sizeof(path), "%s:/config/ultrahand/notifications/%s-%s-%" PRIu64 "%02u.notify", ams::fs::impl::SdCardFileSystemMountName,
                           AppName, icon.state, os::GetSystemTick().GetInt64Value(), static_cast<unsigned>(g_sequence++ % 100));

            if (const Result result = WriteNewFile(path, json, len); R_FAILED(result)) {
                Log("notify: cannot write %s 2%03d-%04d", icon.state, result.GetModule(), result.GetDescription());
            }
        }

        void FormatCapacity(char *dst, size_t dst_size, u64 capacity_bytes) {
            if (capacity_bytes >= 1_GB) {
                util::SNPrintf(dst, dst_size, "%" PRIu64 " GB", (capacity_bytes + 1_GB / 2) / 1_GB);
            } else {
                util::SNPrintf(dst, dst_size, "%" PRIu64 " MB", (capacity_bytes + 1_MB / 2) / 1_MB);
            }
        }

    }

    void InitializeNotifications() {
        /* Set up even when the option is off: the Manager can turn it on without a reboot. */
        if (!IsUltrahandInstalled()) {
            Log("notify: unavailable (ultrahand missing)");
            return;
        }

        char path[ams::fs::EntryNameLengthMax + 1];
        FormatSdPath(path, sizeof(path), "/config/ultrahand/notifications");
        R_DISCARD(ams::fs::CreateDirectory(path));
        FormatSdPath(path, sizeof(path), "/config/ultrahand/assets/notifications");
        R_DISCARD(ams::fs::CreateDirectory(path));

        for (const auto *icon : { std::addressof(ConnectedIcon), std::addressof(RemovedIcon), std::addressof(UnreadableIcon) }) {
            InstallIcon(*icon);
        }

        g_enabled = true;
        Log("notify: ready (locale %s, %s)", i18n::GetLocale(), IsConfigEnabled(ConfigKey::Notifications) ? "on" : "off");
    }

    void NotifyDriveConnected(u64 capacity_bytes, size_t game_count, size_t update_count, size_t add_on_count) {
        char size[32], games[16], updates[16], add_ons[16], text[256];
        FormatCapacity(size, sizeof(size), capacity_bytes);
        util::SNPrintf(games, sizeof(games), "%zu", game_count);
        util::SNPrintf(updates, sizeof(updates), "%zu", update_count);
        util::SNPrintf(add_ons, sizeof(add_ons), "%zu", add_on_count);

        i18n::Format(text, sizeof(text), "Notifications.DriveConnected", { { "SIZE", size }, { "GAME_COUNT", games }, { "UPDATE_COUNT", updates }, { "DLC_COUNT", add_ons } });
        Post(ConnectedIcon, text);
    }

    void NotifyDriveRemoved() {
        char text[256];
        i18n::Format(text, sizeof(text), "Notifications.DriveRemoved");
        Post(RemovedIcon, text);
    }

    void NotifyDriveRequired() {
        constexpr TimeSpan MinimumInterval = TimeSpan::FromSeconds(10);
        static constinit s64 s_last_tick = 0;

        const s64 now = os::GetSystemTick().GetInt64Value();
        if (s_last_tick != 0 && os::ConvertToTimeSpan(os::Tick(now - s_last_tick)) < MinimumInterval) {
            return;
        }
        s_last_tick = now;

        char text[256];
        i18n::Format(text, sizeof(text), "Notifications.DriveRequired");
        Post(RemovedIcon, text);
    }

    void NotifyDriveWithoutGamesFolder() {
        char text[256];
        i18n::Format(text, sizeof(text), "Notifications.DriveWithoutGamesFolder");
        Post(RemovedIcon, text);
    }

    void NotifyDriveUnreadable() {
        char text[256];
        i18n::Format(text, sizeof(text), "Notifications.DriveUnreadable");
        Post(UnreadableIcon, text);
    }

}
