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
 * @file usbl_ns_shim.c
 * @brief ns:am2 commands not wrapped by libnx.
 * @copyright libnx Authors
 */
#include <switch.h>
#include "usbl_ns_shim.h"

Result nsamPushApplicationRecord(Service *s, u64 application_id, u8 last_event, const void *records, size_t records_size) {
    const struct {
        u8 last_event;
        u8 padding[7];
        u64 application_id;
    } in = { last_event, {0}, application_id };

    return serviceDispatchIn(s, 16, in,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
        .buffers = { { records, records_size } },
    );
}

Result nsamInvalidateApplicationControlCache(Service *s, u64 application_id) {
    return serviceDispatchIn(s, 404, application_id);
}

Result nsamDeleteApplicationRecord(Service *s, u64 application_id) {
    return serviceDispatchIn(s, 27, application_id);
}
