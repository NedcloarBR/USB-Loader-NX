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
#include <usbl/protocol.hpp>

namespace ams::mitm::usbl {

    /* Spawns the thread that waits for USB drives, mounts them, indexes their XCIs and tears everything down on detach. */
    void StartUsbVolumeManager();

    using DriveState  = ::usbl::protocol::DriveState;
    using DriveStatus = ::usbl::protocol::DriveStatus;

    /* Drives plugged in and attached right now. */
    s32 GetDriveStatuses(DriveStatus *out, s32 max_count);

    /* Capacity and free space measured at mount. Requires the volume lock. */
    bool GetDriveSpace(s32 drive, u64 *out_free, u64 *out_total);

}
