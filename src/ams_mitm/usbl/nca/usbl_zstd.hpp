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

    /* Decompresses one zstd frame into dst, which it must fill exactly. Unlike util::DecompressZstd, a damaged frame is an
     * error instead of an abort: dumps on a USB drive can be. Not thread safe: the caller serializes calls. */
    Result DecompressZstdFrame(void *dst, size_t dst_size, const void *src, size_t src_size);

    /* Frees the decompression context (allocated again by the next call). */
    void ReleaseZstd();

}
