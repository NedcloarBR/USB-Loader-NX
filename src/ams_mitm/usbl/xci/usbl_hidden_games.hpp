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

    /* Games the user hid from HOME in the Manager, kept in sd:/atmosphere/config/usbloadernx/hidden_games.txt
     * (one application id in hex per line), so they stay hidden across reboots and drive reconnections. */
    void LoadHiddenGames();

    bool IsHiddenGame(ncm::ApplicationId application_id);

    /* Updates the list and rewrites the file (writes to the SD card). */
    Result SetHiddenGame(ncm::ApplicationId application_id, bool hidden);

}
