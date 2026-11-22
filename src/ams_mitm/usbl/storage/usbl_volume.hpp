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

    /* FatFs is not reentrant: every FatFs call, and every read of the content index, must hold this lock. */
    os::SdkRecursiveMutex &GetVolumeLock();

    /* Per drive, bumped on every mount/unmount so storages opened on a previous mount fail instead of reading stale handles. */
    u32 GetVolumeGeneration(s32 drive);
    bool IsVolumeMounted(s32 drive);

    void NotifyVolumeMounted(s32 drive);
    void NotifyVolumeUnmounted(s32 drive);

    /* The volume serial number (as Windows shows it, "A1B2-C3D4") names a drive for homebrew: sdmc:/usbloadernx/usb/<name>/.
     * A second mounted volume with the same serial is "<serial>-2". All of these require the volume lock. */
    constexpr inline size_t VolumeNameLength = 16;
    void SetVolumeSerial(s32 drive, u32 serial);
    u32 GetVolumeSerial(s32 drive);
    void FormatVolumeName(char *out, size_t out_size, s32 drive);
    /* The mounted drive with that name (case does not matter), or -1. */
    s32 FindMountedVolume(const char *name, size_t name_length);
    /* Mounted drives, in drive order. */
    s32 ListMountedVolumes(s32 *out, s32 max_count);

}
