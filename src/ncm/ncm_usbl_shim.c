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
 * @file ncm_usbl_shim.c
 * @brief USB Loader NX ("usbl") IPC wrapper used by ncm.
 * @copyright libnx Authors
 */
#include <switch.h>
#include "ncm_usbl_shim.h"

Result usblGetContentInfoForNcm(Service *s, UsblServedContentInfo *out, const UsblContentQuery *request) {
    return serviceDispatchInOut(s, 4, *request, *out);
}

Result usblReadNcaContent(Service *s, void *dst, size_t size, const UsblContentReadRequest *request) {
    return serviceDispatchIn(s, 5, *request,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
        .buffers = { { dst, size } },
    );
}

Result usblForgetContentForNcm(Service *s, bool *out_was_known, const NcmContentId *content_id) {
    u8 was_known = 0;
    const Result rc = serviceDispatchInOut(s, 8, *content_id, was_known);
    if (R_SUCCEEDED(rc)) {
        *out_was_known = was_known != 0;
    }
    return rc;
}
