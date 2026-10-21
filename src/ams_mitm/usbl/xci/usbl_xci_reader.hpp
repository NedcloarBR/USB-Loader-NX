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
#include "usbl_hfs0.hpp"

namespace ams::mitm::usbl::xci {

    class XciReader {
        NON_COPYABLE(XciReader);
        NON_MOVEABLE(XciReader);
        private:
            FIL *m_file;
            s64 m_header_offset;
            u64 m_package_id;
            Hfs0Partition m_root;
        public:
            XciReader() : m_file(nullptr), m_header_offset(0), m_package_id(0), m_root() { /* ... */ }

            Result Initialize(FIL *file);

            const Hfs0Partition &GetRootPartition() const { return m_root; }
            Result OpenPartition(Hfs0Partition *out, const char *name) const;

            s64 GetHeaderOffset() const { return m_header_offset; }
            u64 GetPackageId() const { return m_package_id; }
    };

}
