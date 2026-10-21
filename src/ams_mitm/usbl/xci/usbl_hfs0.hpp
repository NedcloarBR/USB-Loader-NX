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

namespace ams::mitm::usbl::xci {

    constexpr size_t Sha256HashSize = crypto::Sha256Generator::HashSize;

    struct Hfs0Entry {
        s64 offset;
        s64 size;
        u32 hashed_region_size;
        u8 hash[Sha256HashSize];
        const char *name;
    };

    class Hfs0Partition {
        NON_COPYABLE(Hfs0Partition);
        private:
            std::unique_ptr<u8[]> m_header;
            size_t m_header_size;
            s64 m_data_offset;
            s32 m_entry_count;
            u32 m_string_table_size;
        public:
            Hfs0Partition() : m_header(), m_header_size(0), m_data_offset(0), m_entry_count(0), m_string_table_size(0) { /* ... */ }
            Hfs0Partition(Hfs0Partition &&) = default;
            Hfs0Partition &operator=(Hfs0Partition &&) = default;

            /* Verifies the first hashed_size bytes of the header against expected_hash. */
            Result Initialize(FIL *file, s64 offset, const u8 *expected_hash, size_t hashed_size);

            s32 GetEntryCount() const { return m_entry_count; }
            Result GetEntry(Hfs0Entry *out, s32 index) const;
            Result FindEntry(Hfs0Entry *out, const char *name) const;
    };

    Result VerifyEntryHash(FIL *file, const Hfs0Entry &entry);

}
