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

namespace ams::mitm::usbl::xci {

    /* Writes the content meta of every indexed application to the SD card ncm database and pushes its ns
     * application record, exactly like an installer would (without copying any NCA). Only runs when
     * system_settings.ini has usbloadernx!auto_register = u8!0x1. Must be called without the volume lock. */
    void RegisterIndexedApplicationsIfEnabled();

    /* An application installed on the console or SD card always wins: its USB copy (indexed or only known) is dropped
     * from the index and the known contents, so it is neither registered nor served. Must be called without the volume lock. */
    void ExcludeInstalledApplications();

    /* usbloadernx!hide_when_removed: removes the ns record of each given application no longer indexed on any drive,
     * so HOME stops showing it; plugging its drive in pushes the record again. Must be called without the volume lock. */
    void HideUnpluggedApplications(const ncm::ProgramId *applications, s32 count);

    /* Same, for every known application whose drive is not plugged in (at boot). */
    void HideUnpluggedKnownApplications();

    /* Manager: hides a USB game from HOME (its ns record goes, persisted in hidden_games.txt) or shows it again.
     * Writes the ns records and the SD card. Must be called without the volume lock. */
    Result SetGameHidden(ncm::ApplicationId application_id, bool hidden);

    using GameStatus = ::usbl::protocol::GameStatus;

    /* Every game known from USB, plugged in or not. Must be called without the volume lock. */
    s32 GetGameStatuses(GameStatus *out, s32 max_count);

}
