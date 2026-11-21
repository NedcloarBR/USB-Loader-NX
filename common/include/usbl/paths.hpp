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

/* Where USB Loader NX keeps its files on the SD card, shared by the module and the Manager, and the format version of each
 * one (FORMATS.md). Paths start at the card's root; each side prefixes its own mount ("sdmc:", "sd:"). */
namespace usbl::paths {

    constexpr char Root[]          = "/usbloadernx";
    constexpr char ConfigFile[]    = "/usbloadernx/config.ini";
    constexpr char ManagerFile[]   = "/usbloadernx/manager.ini";
    constexpr char TemplatesDir[]  = "/usbloadernx/templates";
    constexpr char DataDir[]       = "/usbloadernx/data";
    constexpr char KnownContents[] = "/usbloadernx/data/known_contents.bin";
    constexpr char HiddenGames[]   = "/usbloadernx/data/hidden_games.ini";
    constexpr char Favorites[]     = "/usbloadernx/data/favorites.ini";
    constexpr char CacheDir[]      = "/usbloadernx/cache";
    constexpr char IconCacheDir[]  = "/usbloadernx/cache/icons";
    constexpr char ThemesDir[]     = "/usbloadernx/themes";
    constexpr char I18nDir[]       = "/usbloadernx/i18n";
    constexpr char LogsDir[]       = "/usbloadernx/logs";
    constexpr char LogFile[]       = "/usbloadernx/logs/usbloadernx.log";
    /* Reserved for playing from the SD card; not read yet. */
    constexpr char GamesDir[]      = "/usbloadernx/games";

    /* Before 1.0.0: moved into Root on first start (the module moves its own, the Manager its own). */
    namespace legacy {
        constexpr char ModuleDir[]     = "/atmosphere/config/usbloadernx";
        constexpr char ModuleLogFile[] = "/atmosphere/logs/usbloadernx.log";
        constexpr char ManagerDir[]    = "/config/usbloadernx-manager";
    }

}

namespace usbl::formats {

    /* Text files carry their version in a section of their own: "[format]" with "version = N" (theme.json: "Format": { "Version": "N" }).
     * A file without it is version 1. A newer version is read as far as it is understood and never rewritten unasked. */
    constexpr char FormatSection[] = "format";
    constexpr char VersionKey[]    = "version";

    constexpr int Config       = 1;
    constexpr int Manager      = 1;
    constexpr int HiddenGames  = 1;
    constexpr int Favorites    = 1;
    constexpr int Theme        = 1;
    /* Binary: the version is in the file's header. */
    constexpr int KnownContents = 1;

}
