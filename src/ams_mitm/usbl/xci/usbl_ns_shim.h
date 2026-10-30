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
 * @file usbl_ns_shim.h
 * @brief ns:am2 commands not wrapped by libnx.
 * @copyright libnx Authors
 */
#pragma once
#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

/* IApplicationManagerInterface::PushApplicationRecord (16). */
Result nsamPushApplicationRecord(Service *s, u64 application_id, u8 last_event, const void *records, size_t records_size);

/* IApplicationManagerInterface::DeleteApplicationRecord (27). */
Result nsamDeleteApplicationRecord(Service *s, u64 application_id);

/* IApplicationManagerInterface::InvalidateApplicationControlCache (404). */
Result nsamInvalidateApplicationControlCache(Service *s, u64 application_id);

#ifdef __cplusplus
}
#endif
