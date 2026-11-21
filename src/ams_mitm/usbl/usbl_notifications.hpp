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

    void NotifyDriveConnected(u64 capacity_bytes, size_t game_count, size_t update_count, size_t add_on_count);
    void NotifyDriveRemoved();
    /* Some dumps or folders on the drive were not read; the log says which and why. */
    void NotifyFilesLeftOut(size_t count);
    void NotifyDriveUnreadable();
    void NotifyDriveWithoutGamesFolder();

    /* A title on an unplugged drive was launched; rate-limited, since loader mounts the ExeFS several times per launch. */
    void NotifyDriveRequired();

}
