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
#include "../usbl_content_source.hpp"

namespace ams::mitm::usbl::badge {

    /* The badge on icons of games served from USB is controlled by usbloadernx!badge (default on). */
    bool IsEnabled();

    /* Decodes a game icon JPEG, draws the badge for the game's source (game card, NSP, installed; a Z mark when compressed)
     * and re-encodes it. Icons that are not square are left alone (ResultInvalidArgument). */
    Result ApplyToJpeg(std::unique_ptr<u8[]> *out, size_t *out_size, const void *jpeg, size_t jpeg_size, BadgeSource source);

}
