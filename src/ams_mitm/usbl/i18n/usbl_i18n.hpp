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

namespace ams::mitm::usbl::i18n {

    /* Strings live in i18n/locales/<locale>.json (en-US is the source and the fallback), embedded at build time.
     * sd:/usbloadernx/i18n/<locale>.json overrides the embedded file or adds a language.
     * Keys are dotted paths into the JSON ("Notifications.DriveConnected"); values use {{NAME}} placeholders. */
    struct Argument {
        const char *name;
        const char *value;
    };

    void Initialize();
    const char *GetLocale();

    /* Writes the translated string with its placeholders replaced; a missing key yields the key itself. */
    void Format(char *dst, size_t dst_size, const char *key, std::initializer_list<Argument> args = {});

}
