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
#include <switch.h>
#include <string>
#include <vector>
#include <usbl/protocol.hpp>

/* Client of the "usbl" service that USB Loader NX registers inside ams.mitm (see common/include/usbl/protocol.hpp). */
namespace usbl {

    using protocol::ConfigKey;
    using protocol::ConfigValue;
    using protocol::DriveState;
    using protocol::DriveStatus;
    using protocol::GameSource;
    using protocol::GameStatus;
    using protocol::VersionInfo;

    /* Fails right away (without waiting for the service) when USB Loader NX is disabled ([usbloadernx] enable), did not
     * start, or this is not the USB Loader NX build of Atmosphère. */
    Result Initialize();
    void Exit();

    /* Fails (2001-... from the module's command table) on a module older than protocol 1.0, which has no GetVersion. */
    Result GetVersion(VersionInfo *out);
    /* The module's protocol minor version, from the last GetVersion (0 before it). Options it added need at least that. */
    u16 GetModuleMinorVersion();

    Result GetConfigValue(ConfigValue *out, ConfigKey key);
    /* A config.ini from a newer version is only rewritten after SetOverwriteNewerConfig(true) (the user said so). */
    Result SetConfigValue(ConfigKey key, bool enabled);
    void SetOverwriteNewerConfig(bool overwrite);
    /* Protocol 1.1: config.ini `language`, a locale ("pt-BR") or empty for the console's; the module switches its
     * notifications to it at once. */
    Result GetLanguage(std::string *out);
    Result SetLanguage(const std::string &locale);
    Result ListDrives(std::vector<DriveStatus> *out);
    Result ListGames(std::vector<GameStatus> *out);
    Result SetGameHidden(u64 application_id, bool hidden);
    Result ShowGameForLaunch(u64 application_id);
    Result ApplyHomeVisibility();
    /* Lets games taken off HOME by a badge change come back, now that the Manager is closing. */
    Result FinishHomeRefresh();
    /* The game's own icon (no badge), while its drive is plugged in. */
    Result GetGameIcon(u64 application_id, std::vector<u8> *out);
    /* "1.0.2", read from the game on the drive (the update's when there is one), while it is plugged in. */
    Result GetGameDisplayVersion(u64 application_id, std::string *out);

}
