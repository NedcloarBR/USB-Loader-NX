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

/* Moves the Manager's files from where versions before 1.0.0 kept them (sdmc:/config/usbloadernx-manager/) into
 * sdmc:/usbloadernx/, converting favorites.txt. Writes to the SD card. Call before settings::Load. */
namespace migration {

    void MoveLegacyFiles();

}
