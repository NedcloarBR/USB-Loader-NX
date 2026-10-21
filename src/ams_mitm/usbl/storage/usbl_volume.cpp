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

}
