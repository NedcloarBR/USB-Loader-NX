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

namespace ams::mitm::usbl::nca {

    /* Sets up a small fssystem (buffer pool, buffer manager, creators) on top of InitializeCrypto. Reserves no keyslots. */
    void InitializeFileSystem();

    /* Validates the NCA header (signature, key derivation) and returns a reader for it. */
    Result OpenReader(std::shared_ptr<fssystem::NcaReader> *out, std::shared_ptr<ams::fs::IStorage> raw_nca);
    /* Same, telling whether the header is re-signed (not Nintendo's signature, accepted for USB content). */
    Result OpenReader(std::shared_ptr<fssystem::NcaReader> *out, bool *out_is_resigned, std::shared_ptr<ams::fs::IStorage> raw_nca);

    /* Mounts a PartitionFs or RomFs section of the NCA, with its integrity layers. */
    Result OpenSectionFileSystem(std::shared_ptr<ams::fs::fsa::IFileSystem> *out, std::shared_ptr<fssystem::NcaReader> reader, s32 section_index);

}
