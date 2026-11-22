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

extern "C" {
#include "../fatfs/ff.h"
}

namespace ams::mitm::usbl {

    /* Read-only view of [offset, offset + size) of a file on the USB volume, owning its own FatFs handle. */
    class UsbFileStorage : public ams::fs::IStorage, public ams::fs::impl::Newable {
        NON_COPYABLE(UsbFileStorage);
        NON_MOVEABLE(UsbFileStorage);
        private:
            FIL m_file;
            std::unique_ptr<DWORD[]> m_link_map;
            s32 m_drive;
            s64 m_offset;
            s64 m_size;
            u32 m_generation;
            bool m_is_open;
        public:
            UsbFileStorage() : m_file(), m_link_map(), m_drive(0), m_offset(0), m_size(0), m_generation(0), m_is_open(false) { /* ... */ }
            virtual ~UsbFileStorage();

            /* path includes the drive's volume prefix ("1:/usbloadernx/games/..."). */
            Result Initialize(s32 drive, const char *path, s64 offset, s64 size);

            virtual Result Read(s64 offset, void *buffer, size_t size) override;
            virtual Result GetSize(s64 *out) override;

            virtual Result Write(s64 offset, const void *buffer, size_t size) override;
            virtual Result Flush() override;
            virtual Result SetSize(s64 size) override;
            virtual Result OperateRange(void *dst, size_t dst_size, ams::fs::OperationId op_id, s64 offset, s64 size, const void *src, size_t src_size) override;
        private:
            bool IsHandleValid() const;
    };

    Result ConvertFatFsResult(FRESULT res);

    /* Requires the volume lock. */
    Result ReadFileAt(FIL *file, s64 offset, void *dst, size_t size);

    /* Requires the volume lock. Installs a fast-seek map on the open file; nullptr when it would be too large. */
    std::unique_ptr<DWORD[]> CreateLinkMap(FIL *file);

    /* Requires the volume lock. Number of separate cluster runs of the open file, or -1 when too many to map. */
    s32 CountFileFragments(FIL *file);

}
