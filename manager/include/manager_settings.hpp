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
#include <string>
#include <switch.h>

/* The Manager's own options (not the module's config.ini): sdmc:/usbloadernx/manager.ini, "key = value" lines under
 * [manager] after its [format] section. Read once at start; every change rewrites the file, unless a newer Manager wrote it. */
namespace settings {

    void Load();
    /* manager.ini comes from a newer Manager: changes are kept until the Manager closes, not written. */
    bool IsFromNewerVersion();

    s32 GetInt(const char *key, s32 default_value);
    bool SetInt(const char *key, s32 value);

    std::string GetString(const char *key, const std::string &default_value);
    bool SetString(const char *key, const std::string &value);

}
