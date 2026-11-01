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
     * application record, exactly like an installer would (without copying any NCA). With usbloadernx!auto_register
     * off it hides every known application instead. Must be called without the volume lock. */
    void RegisterIndexedApplicationsIfEnabled();

    /* An application installed on the console or SD card always wins: its USB copy (indexed or only known) is dropped
     * from the index and the known contents, so it is neither registered nor served. Must be called without the volume lock. */
    void ExcludeInstalledApplications();

    /* usbloadernx!hide_when_removed: removes the ns record of each given application no longer indexed on any drive,
     * so HOME stops showing it; plugging its drive in pushes the record again. Must be called without the volume lock. */
    void HideUnpluggedApplications(const ncm::ProgramId *applications, s32 count);

    /* Puts HOME in line with the options, for every known application: shown when auto_register is on, it is not hidden
     * in the Manager, and its drive is plugged in or hide_when_removed is off; otherwise its ns record is removed (the
     * content metas stay, so showing it again only takes a push). Must be called without the volume lock. */
    void ApplyHomeVisibility();

    /* Pushes every plugged-in application again, even if already pushed since boot, so ns re-reads its icon (badge).
     * Must be called without the volume lock. */
    void RefreshApplicationRecords();

    /* Manager: hides a USB game from HOME (its ns record goes, persisted in hidden_games.txt) or shows it again.
     * Writes the ns records and the SD card. Must be called without the volume lock. */
    Result SetGameHidden(ncm::ApplicationId application_id, bool hidden);

    using GameStatus = ::usbl::protocol::GameStatus;

    /* Every game known from USB, plugged in or not. Must be called without the volume lock. */
    s32 GetGameStatuses(GameStatus *out, s32 max_count);

}
