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
#include <usbl/protocol.hpp>

namespace ams::mitm::usbl {

    /* Every preference except `enable` and `debug` (system_settings.ini) lives in sd:/usbloadernx/config.ini, written by
     * the Manager and created with the defaults when missing. Values are part of the usbl service wire format. */
    using ConfigKey   = ::usbl::protocol::ConfigKey;
    using ConfigValue = ::usbl::protocol::ConfigValue;

    /* (Re)reads config.ini, creating it with the defaults when it does not exist. Requires the SD card. */
    void LoadConfig();

    ConfigValue GetConfigValue(ConfigKey key);
    /* Rewrites config.ini with every option (this one changed) and applies it at once. Writes to the SD card.
     * A config.ini from a newer version is only rewritten with overwrite (ResultNewerFormat otherwise). */
    Result SetConfigValue(ConfigKey key, bool enabled, bool overwrite);
    bool IsConfigFromNewerVersion();
    bool IsConfigEnabled(ConfigKey key);

    /* `language` in config.ini: a locale ("pt-BR"), or empty for the console's language. */
    void GetLanguage(char *dst, size_t dst_size);
    /* Rewrites config.ini like SetConfigValue; the caller reloads the strings (i18n::Initialize). */
    Result SetLanguage(const char *locale, bool overwrite);

}
