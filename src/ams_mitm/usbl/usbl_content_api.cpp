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
#include <stratosphere.hpp>
#include "usbl_content_api.hpp"
#include "xci/usbl_content_index.hpp"
#include "xci/usbl_known_contents.hpp"
#include "usbl_results.hpp"

namespace ams::mitm::usbl {

    namespace {

        /* A known content whose drive is not plugged in reports DriveNotConnected instead of "not found". */
        ServedContent ToServedContent(const KnownContent &known) {
            return { .content_id = known.content_id, .program_id = known.program_id, .rights_id = known.rights_id, .key_generation = known.key_generation, .source = known.source };
        }

        Result ToServedResult(Result result, bool is_known) {
            R_UNLESS(!(usbl::ResultContentNotFound::Includes(result) && is_known), usbl::ResultDriveNotConnected());
            R_RETURN(result);
        }

    }

    bool FindServedContent(ServedContent *out, ncm::ContentId content_id) {
        KnownContent known;
        if (!FindKnownContent(std::addressof(known), content_id)) {
            return false;
        }

        *out = ToServedContent(known);
        return true;
    }

    bool FindServedProgramContent(ServedContent *out, ncm::ProgramId program_id, fssystem::NcaHeader::ContentType content_type) {
        KnownContent known;
        if (!FindKnownProgramContent(std::addressof(known), program_id, static_cast<u8>(content_type), ContentLayer::Current)) {
            return false;
        }

        *out = ToServedContent(known);
        return true;
    }

    bool IsServedProgram(ncm::ProgramId program_id) {
        return IsKnownProgram(program_id);
    }

    bool IsServedPatchedProgram(ncm::ProgramId program_id) {
        return IsKnownPatchedProgram(program_id);
    }

    Result OpenServedContentStorage(std::shared_ptr<ams::fs::IStorage> *out, ncm::ContentId content_id) {
        KnownContent known;
        R_RETURN(ToServedResult(OpenIndexedContentStorage(out, content_id), FindKnownContent(std::addressof(known), content_id)));
    }

    Result OpenServedProgramContentStorage(std::shared_ptr<ams::fs::IStorage> *out, ncm::ProgramId program_id, fssystem::NcaHeader::ContentType content_type) {
        R_RETURN(ToServedResult(OpenIndexedContentStorage(out, program_id, content_type, ContentLayer::Current), IsKnownProgram(program_id)));
    }

    Result OpenServedBaseProgramContentStorage(std::shared_ptr<ams::fs::IStorage> *out, ncm::ProgramId program_id, fssystem::NcaHeader::ContentType content_type) {
        R_RETURN(ToServedResult(OpenIndexedContentStorage(out, program_id, content_type, ContentLayer::Base), IsKnownProgram(program_id)));
    }

}
