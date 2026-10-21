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
#include "usbl_disk_io.hpp"

extern "C" {
#include "fatfs/ff.h"
#include "fatfs/diskio.h"
}

namespace ams::mitm::usbl {

    namespace {

        constinit usb::MassStorageDevice *g_devices[MaxDrives] = {};

    }

    void BindDiskDevice(s32 drive, usb::MassStorageDevice *device) {
        AMS_ASSERT(0 <= drive && drive < MaxDrives);
        g_devices[drive] = device;
    }

    void UnbindDiskDevice(s32 drive) {
        AMS_ASSERT(0 <= drive && drive < MaxDrives);
        g_devices[drive] = nullptr;
    }

    usb::MassStorageDevice *GetDiskDevice(BYTE pdrv) {
        return pdrv < MaxDrives ? g_devices[pdrv] : nullptr;
    }

}

using ams::mitm::usbl::GetDiskDevice;

extern "C" {

    DSTATUS disk_status(BYTE pdrv) {
        return GetDiskDevice(pdrv) != nullptr ? 0 : STA_NOINIT;
    }

    DSTATUS disk_initialize(BYTE pdrv) {
        return disk_status(pdrv);
    }

    DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
        if (disk_status(pdrv) != 0) {
            return RES_NOTRDY;
        }
        return R_SUCCEEDED(GetDiskDevice(pdrv)->Read(buff, sector, count)) ? RES_OK : RES_ERROR;
    }

    DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
        if (disk_status(pdrv) != 0) {
            return RES_NOTRDY;
        }

        switch (cmd) {
            case CTRL_SYNC:
                return RES_OK;
            case GET_SECTOR_COUNT:
                *static_cast<LBA_t *>(buff) = GetDiskDevice(pdrv)->GetBlockCount();
                return RES_OK;
            case GET_SECTOR_SIZE:
                *static_cast<WORD *>(buff) = static_cast<WORD>(GetDiskDevice(pdrv)->GetBlockSize());
                return RES_OK;
            default:
                return RES_PARERR;
        }
    }

}
