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

namespace ams::mitm::usbl::xci {

    /* Validates the XCI and adds every verified NCA of its secure partition to the content index. Requires the volume lock. */
    void IndexXci(s32 drive, const char *path);

    enum class NspKind {
        Unknown,
        Application,
        Patch,
        AddOnContent,
    };

    /* Adds every NCA of an NSP (PFS0) whose header signature verifies. PFS0 carries no hashes; the NCA hash layers
     * still verify every read. Returns what the NSP holds, from its meta's title id. Requires the volume lock. */
    NspKind IndexNsp(s32 drive, const char *path);

    /* Opens each indexed NCA through the same path IPC clients use and re-validates its header. */
    void SelfTestContentIndex();

}
