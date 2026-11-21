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

namespace ams::mitm::usbl {

    /* Games the user hid from HOME in the Manager, kept in sd:/usbloadernx/data/hidden_games.ini ("[hidden]" with
     * "<application id> = 1"), so they stay hidden across reboots and drive reconnections. */
    void LoadHiddenGames();
    bool IsHiddenGamesFromNewerVersion();

    /* The pre-1.0.0 hidden_games.txt (one id per line), saved in the new format. */
    void ConvertLegacyHiddenGames(const char *legacy_text, size_t size);

    bool IsHiddenGame(ncm::ApplicationId application_id);

    /* Updates the list and rewrites the file (writes to the SD card); ResultNewerFormat for a newer version's file. */
    Result SetHiddenGame(ncm::ApplicationId application_id, bool hidden);

}
