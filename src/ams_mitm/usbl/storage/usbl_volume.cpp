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
#include "usbl_volume.hpp"
#include "../usbl_disk_io.hpp"

namespace ams::mitm::usbl {

    namespace {

        constinit os::SdkRecursiveMutex g_volume_lock;
        constinit u32 g_volume_generations[MaxDrives] = {};
        constinit bool g_volume_mounted[MaxDrives] = {};
        constinit u32 g_volume_serials[MaxDrives] = {};

    }

    os::SdkRecursiveMutex &GetVolumeLock() {
        return g_volume_lock;
    }

    u32 GetVolumeGeneration(s32 drive) {
        AMS_ASSERT(g_volume_lock.IsLockedByCurrentThread());
        AMS_ASSERT(0 <= drive && drive < MaxDrives);
        return g_volume_generations[drive];
    }

    bool IsVolumeMounted(s32 drive) {
        AMS_ASSERT(g_volume_lock.IsLockedByCurrentThread());
        AMS_ASSERT(0 <= drive && drive < MaxDrives);
        return g_volume_mounted[drive];
    }

    void NotifyVolumeMounted(s32 drive) {
        AMS_ASSERT(g_volume_lock.IsLockedByCurrentThread());
        ++g_volume_generations[drive];
        g_volume_mounted[drive] = true;
    }

    void NotifyVolumeUnmounted(s32 drive) {
        AMS_ASSERT(g_volume_lock.IsLockedByCurrentThread());
        ++g_volume_generations[drive];
        g_volume_mounted[drive] = false;
    }

    void SetVolumeSerial(s32 drive, u32 serial) {
        AMS_ASSERT(g_volume_lock.IsLockedByCurrentThread());
        AMS_ASSERT(0 <= drive && drive < MaxDrives);
        g_volume_serials[drive] = serial;
    }

    u32 GetVolumeSerial(s32 drive) {
        AMS_ASSERT(g_volume_lock.IsLockedByCurrentThread());
        AMS_ASSERT(0 <= drive && drive < MaxDrives);
        return g_volume_serials[drive];
    }

    void FormatVolumeName(char *out, size_t out_size, s32 drive) {
        AMS_ASSERT(g_volume_lock.IsLockedByCurrentThread());
        const u32 serial = g_volume_serials[drive];

        /* Two volumes with the same serial (one copied sector by sector from the other): the later drive gets a suffix. */
        s32 same = 0;
        for (s32 other = 0; other < drive; ++other) {
            same += g_volume_mounted[other] && g_volume_serials[other] == serial ? 1 : 0;
        }

        if (same == 0) {
            util::SNPrintf(out, out_size, "%04X-%04X", serial >> 16, serial & 0xFFFF);
        } else {
            util::SNPrintf(out, out_size, "%04X-%04X-%d", serial >> 16, serial & 0xFFFF, same + 1);
        }
    }

    s32 FindMountedVolume(const char *name, size_t name_length) {
        AMS_ASSERT(g_volume_lock.IsLockedByCurrentThread());
        for (s32 drive = 0; drive < MaxDrives; ++drive) {
            if (!g_volume_mounted[drive]) {
                continue;
            }
            char volume_name[VolumeNameLength];
            FormatVolumeName(volume_name, sizeof(volume_name), drive);
            if (std::strlen(volume_name) == name_length && util::Strnicmp(volume_name, name, static_cast<int>(name_length)) == 0) {
                return drive;
            }
        }
        return -1;
    }

    s32 ListMountedVolumes(s32 *out, s32 max_count) {
        AMS_ASSERT(g_volume_lock.IsLockedByCurrentThread());
        s32 count = 0;
        for (s32 drive = 0; drive < MaxDrives && count < max_count; ++drive) {
            if (g_volume_mounted[drive]) {
                out[count++] = drive;
            }
        }
        return count;
    }

}
