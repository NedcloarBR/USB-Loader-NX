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
/**
 * @file ldr_usbl_shim.c
 * @brief USB Loader NX ("usbl") IPC wrapper used by loader.
 * @copyright libnx Authors
 */
#include <switch.h>
#include "ldr_usbl_shim.h"

Result ldrUsblIsServedProgram(Service *s, bool *out, u64 program_id) {
    u8 tmp = 0;
    Result rc = serviceDispatchInOut(s, 0, program_id, tmp);
    if (R_SUCCEEDED(rc)) *out = tmp != 0;
    return rc;
}

Result ldrUsblOpenCodeFileSystem(Service *s, FsFileSystem *out, void *verification_data, size_t verification_data_size, u64 program_id) {
    return serviceDispatchIn(s, 6, program_id,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
        .buffers = { { verification_data, verification_data_size } },
        .out_num_objects = 1,
        .out_objects = &out->s,
    );
}
