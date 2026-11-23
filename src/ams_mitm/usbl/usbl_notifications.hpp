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
#include <stratosphere.hpp>

namespace ams::mitm::usbl {

    /* Ultrahand API notifications: a .notify JSON in sd:/config/ultrahand/notifications/ plus a 50x50 RGBA icon.
     * Only written when Ultrahand is installed and usbloadernx!notifications is enabled. */
    void InitializeNotifications();

    /* What the module learned about a drive by indexing it. */
    struct DriveSummary {
        const char *name;
        u64 capacity;
        bool is_exfat;
        bool has_games_folder;
        u32 volume_serial;
        size_t games;
        size_t updates;
        size_t add_ons;
        /* Dumps or folders not read; the Manager and the log say which and why. */
        size_t left_out;
        /* Of those, the NSZ and XCZ compressed in solid mode. */
        size_t solid_compressed;
    };

    /* Each one is a title and a short list. */

    /* A drive was found and is being read; a hard drive can take a while to spin up, a large drive to index. */
    void NotifyDriveReading();
    void NotifyDriveConnected(const DriveSummary &drive);
    void NotifyDriveRemoved(const char *name);
    /* The file system is neither exFAT nor FAT32. */
    void NotifyDriveUnreadable(const char *name);
    /* The drive never answered (a hard drive without enough power, usually); it is skipped until plugged in again. */
    void NotifyDriveNotOpened();

    /* A title on an unplugged drive was launched; rate-limited, since loader mounts the ExeFS several times per launch. */
    void NotifyDriveRequired();

}
