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
 * @file ldr_usbl_shim.h
 * @brief USB Loader NX ("usbl") IPC wrapper used by loader.
 * @copyright libnx Authors
 */
#pragma once
#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

Result ldrUsblIsServedProgram(Service *s, bool *out, u64 program_id);
Result ldrUsblOpenCodeFileSystem(Service *s, FsFileSystem *out, void *verification_data, size_t verification_data_size, u64 program_id);

#ifdef __cplusplus
}
#endif
