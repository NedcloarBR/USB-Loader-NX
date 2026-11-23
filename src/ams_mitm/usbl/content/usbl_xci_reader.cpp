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
#include "usbl_xci_reader.hpp"
#include "../storage/usbl_usb_file_storage.hpp"
#include "../usbl_results.hpp"

namespace ams::mitm::usbl::content {

    namespace {

        constexpr u32 GameCardHeaderMagic = util::FourCC<'H', 'E', 'A', 'D'>::Code;

        /* Some dumpers prepend the 0x1000-byte card key area (initial data) to the image. */
        constexpr s64 HeaderOffsets[] = { 0x0, 0x1000 };

        struct GameCardHeader {
            u8 signature[0x100];
            u32 magic;
            u32 secure_area_start_page;
            u32 backup_area_start_page;
            u8 key_index;
            u8 rom_size;
            u8 header_version;
            u8 flags;
            u64 package_id;
            u32 valid_data_end_page;
            u32 reserved_11c;
            u8 iv[0x10];
            u64 partition_fs_header_address;
            u64 partition_fs_header_size;
            u8 partition_fs_header_hash[Sha256HashSize];
            u8 initial_data_hash[Sha256HashSize];
            u8 reserved_180[0x80];
        };
        static_assert(sizeof(GameCardHeader) == 0x200);
        static_assert(AMS_OFFSETOF(GameCardHeader, partition_fs_header_address) == 0x130);

    }

    Result XciReader::Initialize(FIL *file, bool verify_hashes) {
        GameCardHeader header;
        bool found = false;
        for (const s64 offset : HeaderOffsets) {
            if (R_SUCCEEDED(ReadFileAt(file, offset, std::addressof(header), sizeof(header))) && header.magic == GameCardHeaderMagic) {
                m_header_offset = offset;
                found = true;
                break;
            }
        }
        R_UNLESS(found, usbl::ResultInvalidXciHeader());

        const s64 root_offset = m_header_offset + static_cast<s64>(header.partition_fs_header_address);
        R_TRY(m_root.Initialize(file, root_offset, verify_hashes ? header.partition_fs_header_hash : nullptr, header.partition_fs_header_size));

        m_file          = file;
        m_verify_hashes = verify_hashes;
        m_package_id = header.package_id;
        R_SUCCEED();
    }

    Result XciReader::OpenPartition(Hfs0Partition *out, const char *name) const {
        Hfs0Entry entry;
        R_TRY(m_root.FindEntry(std::addressof(entry), name));

        Hfs0Partition partition;
        R_TRY(partition.Initialize(m_file, entry.offset, m_verify_hashes ? entry.hash : nullptr, entry.hashed_region_size));

        *out = std::move(partition);
        R_SUCCEED();
    }

}
