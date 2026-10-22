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
 * @file ncm_usbl_shim.h
 * @brief USB Loader NX ("usbl") IPC wrapper used by ncm.
 * @copyright libnx Authors
 */
#pragma once
#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mirrors ams::mitm::usbl::ServedContentInfo. */
typedef struct {
    s64 size;
    u64 program_id;
    bool is_served;
    u8 key_generation;
    u8 reserved[6];
    u8 rights_id[0x10];
} UsblServedContentInfo;

/* Mirrors ams::mitm::usbl::ContentQuery. */
typedef struct {
    NcmContentId content_id;
    u32 query;
    u32 reserved;
} UsblContentQuery;

Result usblGetContentInfoForNcm(Service *s, UsblServedContentInfo *out, const UsblContentQuery *request);

/* Mirrors ams::mitm::usbl::ContentReadRequest. */
typedef struct {
    NcmContentId content_id;
    s64 offset;
} UsblContentReadRequest;

Result usblReadNcaContent(Service *s, void *dst, size_t size, const UsblContentReadRequest *request);

Result usblForgetContentForNcm(Service *s, bool *out_was_known, const NcmContentId *content_id);

#ifdef __cplusplus
}
#endif
