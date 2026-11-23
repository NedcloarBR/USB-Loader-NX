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
#include "usbl_zstd.hpp"
#include "../usbl_results.hpp"

/* The zstd libstratosphere is built with, compiled again here with internal linkage: libstratosphere keeps its symbols static. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define ZSTD_STATIC_LINKING_ONLY
#define ZSTDLIB_VISIBLE static
#define ZSTDLIB_HIDDEN  static
#include "../../../../atmosphere/libraries/libstratosphere/source/util/zstd.h"
#include "../../../../atmosphere/libraries/libstratosphere/source/util/zstd.inc"
#pragma GCC diagnostic pop

namespace ams::mitm::usbl::nca {

    namespace {

        constinit ZSTD_DCtx *g_context = nullptr;

    }

    Result DecompressZstdFrame(void *dst, size_t dst_size, const void *src, size_t src_size) {
        if (g_context == nullptr) {
            g_context = ZSTD_createDCtx();
            R_UNLESS(g_context != nullptr, fs::ResultAllocationMemoryFailedNew());
        }

        const size_t written = ZSTD_decompressDCtx(g_context, dst, dst_size, src, src_size);
        R_UNLESS(!ZSTD_isError(written) && written == dst_size, usbl::ResultDecompressionFailed());
        R_SUCCEED();
    }

    void ReleaseZstd() {
        if (g_context != nullptr) {
            ZSTD_freeDCtx(g_context);
            g_context = nullptr;
        }
    }

}
