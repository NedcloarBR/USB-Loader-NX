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
#include "usbl_content_source.hpp"

namespace ams::mitm::usbl {

    /* In-process API for fs_mitm. Titles indexed at any time are "served" (from usbl_known_contents); opening their
     * content without the drive plugged in fails with ResultDriveNotConnected. Nothing is served while disabled. */
    struct ServedContent {
        ncm::ContentId content_id;
        ncm::ProgramId program_id;
        ams::fs::RightsId rights_id;
        u8 key_generation;
        ContentSource source;
    };

    bool FindServedContent(ServedContent *out, ncm::ContentId content_id);
    bool FindServedProgramContent(ServedContent *out, ncm::ProgramId program_id, fssystem::NcaHeader::ContentType content_type);
    bool IsServedProgram(ncm::ProgramId program_id);
    /* An update is served with the application: its Program's RomFS only holds changes on top of the base game's. */
    bool IsServedPatchedProgram(ncm::ProgramId program_id);

    /* content_type is an fssystem::NcaHeader::ContentType. Returns the raw (still encrypted) NCA bytes. */
    Result OpenServedContentStorage(std::shared_ptr<ams::fs::IStorage> *out, ncm::ContentId content_id);
    /* The update's content when one is served, the base game's otherwise. */
    Result OpenServedProgramContentStorage(std::shared_ptr<ams::fs::IStorage> *out, ncm::ProgramId program_id, fssystem::NcaHeader::ContentType content_type);
    Result OpenServedBaseProgramContentStorage(std::shared_ptr<ams::fs::IStorage> *out, ncm::ProgramId program_id, fssystem::NcaHeader::ContentType content_type);

}
