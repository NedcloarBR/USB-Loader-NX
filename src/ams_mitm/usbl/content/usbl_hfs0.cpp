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
#include "usbl_hfs0.hpp"
#include "../storage/usbl_usb_file_storage.hpp"
#include "../usbl_results.hpp"

namespace ams::mitm::usbl::content {

    namespace {

        constexpr u32 Hfs0Magic = util::FourCC<'H', 'F', 'S', '0'>::Code;

        struct Hfs0Header {
            u32 magic;
            u32 entry_count;
            u32 string_table_size;
            u32 reserved;
        };
        static_assert(sizeof(Hfs0Header) == 0x10);

        struct Hfs0RawEntry {
            u64 offset;
            u64 size;
            u32 name_offset;
            u32 hashed_region_size;
            u64 reserved;
            u8 hash[Sha256HashSize];
        };
        static_assert(sizeof(Hfs0RawEntry) == 0x40);

        constexpr u32 MaxEntryCount      = 0x1000;
        constexpr u32 MaxStringTableSize = 256_KB;
        constexpr u32 MaxHashedRegionSize = 1_MB;

        bool VerifySha256(const void *data, size_t size, const u8 *expected) {
            u8 hash[Sha256HashSize];
            crypto::GenerateSha256(hash, sizeof(hash), data, size);
            return crypto::IsSameBytes(hash, expected, sizeof(hash));
        }

    }

    Result Hfs0Partition::Initialize(FIL *file, s64 offset, const u8 *expected_hash, size_t hashed_size) {
        Hfs0Header header;
        R_TRY(ReadFileAt(file, offset, std::addressof(header), sizeof(header)));

        R_UNLESS(header.magic == Hfs0Magic,                     usbl::ResultInvalidHfs0Header());
        R_UNLESS(header.entry_count <= MaxEntryCount,           usbl::ResultInvalidHfs0Header());
        R_UNLESS(header.string_table_size <= MaxStringTableSize, usbl::ResultInvalidHfs0Header());

        const size_t header_size = sizeof(Hfs0Header) + header.entry_count * sizeof(Hfs0RawEntry) + header.string_table_size;
        R_UNLESS(hashed_size <= MaxHashedRegionSize, usbl::ResultInvalidHfs0Header());

        /* The hashed region may include padding past the string table. */
        const size_t read_size = std::max(header_size, hashed_size);
        std::unique_ptr<u8[]> buffer(new (std::nothrow) u8[read_size + 1]);
        R_UNLESS(buffer != nullptr, fs::ResultAllocationMemoryFailedNew());
        R_TRY(ReadFileAt(file, offset, buffer.get(), read_size));
        buffer[read_size] = '\x00';

        R_UNLESS(VerifySha256(buffer.get(), hashed_size, expected_hash), usbl::ResultHashMismatch());

        m_header            = std::move(buffer);
        m_header_size       = header_size;
        m_data_offset       = offset + header_size;
        m_entry_count       = header.entry_count;
        m_string_table_size = header.string_table_size;
        R_SUCCEED();
    }

    Result Hfs0Partition::GetEntry(Hfs0Entry *out, s32 index) const {
        R_UNLESS(0 <= index && index < m_entry_count, usbl::ResultOutOfRange());

        Hfs0RawEntry raw;
        std::memcpy(std::addressof(raw), m_header.get() + sizeof(Hfs0Header) + index * sizeof(Hfs0RawEntry), sizeof(raw));
        R_UNLESS(raw.name_offset < m_string_table_size,          usbl::ResultInvalidHfs0Header());
        R_UNLESS(raw.hashed_region_size <= MaxHashedRegionSize,  usbl::ResultInvalidHfs0Header());
        R_UNLESS(raw.hashed_region_size <= raw.size,             usbl::ResultInvalidHfs0Header());

        const char *string_table = reinterpret_cast<const char *>(m_header.get() + m_header_size - m_string_table_size);
        const size_t max_name_len = m_string_table_size - raw.name_offset;
        R_UNLESS(::strnlen(string_table + raw.name_offset, max_name_len) < max_name_len, usbl::ResultInvalidHfs0Header());

        out->offset             = m_data_offset + static_cast<s64>(raw.offset);
        out->size               = static_cast<s64>(raw.size);
        out->hashed_region_size = raw.hashed_region_size;
        out->name               = string_table + raw.name_offset;
        std::memcpy(out->hash, raw.hash, sizeof(out->hash));
        R_SUCCEED();
    }

    Result Hfs0Partition::FindEntry(Hfs0Entry *out, const char *name) const {
        for (s32 i = 0; i < m_entry_count; ++i) {
            R_TRY(this->GetEntry(out, i));
            if (std::strcmp(out->name, name) == 0) {
                R_SUCCEED();
            }
        }
        R_THROW(usbl::ResultPartitionNotFound());
    }

    Result VerifyEntryHash(FIL *file, const Hfs0Entry &entry) {
        std::unique_ptr<u8[]> buffer(new (std::nothrow) u8[entry.hashed_region_size]);
        R_UNLESS(buffer != nullptr, fs::ResultAllocationMemoryFailedNew());

        R_TRY(ReadFileAt(file, entry.offset, buffer.get(), entry.hashed_region_size));
        R_UNLESS(VerifySha256(buffer.get(), entry.hashed_region_size, entry.hash), usbl::ResultHashMismatch());
        R_SUCCEED();
    }

}
