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
#include "usbl_content_index.hpp"

namespace ams::mitm::usbl {

    /* Every NCA ever indexed, persisted on the SD card, so a title whose drive is unplugged still "exists" for
     * ncm and ns: launching it fails with DriveNotConnected instead of ns marking it as corrupted. */
    struct KnownContent {
        ncm::ContentId content_id;
        ncm::ProgramId program_id;
        fs::RightsId rights_id;
        s64 size;
        u8 content_type;
        u8 key_generation;
        ContentSource source;
        bool is_patch;
        u8 reserved[4];
    };
    static_assert(sizeof(KnownContent) == 0x38);

    /* Requires the SD card (call after mitm::WaitInitialized). */
    void LoadKnownContents();

    /* Merges the contents indexed from the mounted drive and rewrites the SD file only when something changed.
     * Must be called without the volume lock. */
    void RememberIndexedContents();

    /* Forgets an application and its update, now installed on the console or SD card (rewrites the SD file if it was known). */
    void ForgetKnownProgram(ncm::ProgramId program_id);
    /* Forgets only the application's update. */
    void ForgetKnownPatch(ncm::ProgramId program_id);

    /* Forgets one content deleted through ncm (installer replacing it, or the title deleted); returns whether it was known. */
    bool ForgetKnownContent(ncm::ContentId content_id);

    /* Program ids of the known applications (their Meta contents; updates and add-on contents excluded). */
    s32 GetKnownApplications(ncm::ProgramId *out, s32 max_count);

    bool FindKnownContent(KnownContent *out, ncm::ContentId content_id);
    bool FindKnownProgramContent(KnownContent *out, ncm::ProgramId program_id, u8 content_type, ContentLayer layer);
    bool IsKnownProgram(ncm::ProgramId program_id);
    /* Whether the application's update Program is known: its RomFS then has to be layered over the base game's. */
    bool IsKnownPatchedProgram(ncm::ProgramId program_id);

}
