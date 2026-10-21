/*
 * Copyright (c) Atmosphère-NX
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
#include <stratosphere/ncm/ncm_content_id.hpp>
#include <stratosphere/ncm/ncm_ids.hpp>
#include <stratosphere/fs/fs_rights_id.hpp>

namespace ams::ncm {

    /* Content that is registered in the SD card database but whose NCA lives outside the SD card (e.g. on a USB drive). */
    struct ExternalContentInfo {
        s64 size;
        ProgramId program_id;
        fs::RightsId rights_id;
        u8 key_generation;
    };

    /* Which content storage command is asking (values are part of the ncm <-> USB Loader NX wire format). */
    enum class ExternalContentQuery : u32 {
        Has               = 1,
        GetSize           = 2,
        GetRightsId       = 3,
        ReadContentIdFile = 5,
        GetProgramId      = 7,
    };

    using FindExternalContentFunction = bool (*)(ExternalContentInfo *out, ContentId content_id, ExternalContentQuery query);
    using ReadExternalContentFunction = Result (*)(void *dst, size_t size, ContentId content_id, s64 offset);
    /* Returns whether the content was external (and is now forgotten). */
    using ForgetExternalContentFunction = bool (*)(ContentId content_id);

    /* Only the ncm process sets these; when unset, behavior is identical to upstream. */
    void SetFindExternalContentFunction(FindExternalContentFunction func);
    void SetReadExternalContentFunction(ReadExternalContentFunction func);
    void SetForgetExternalContentFunction(ForgetExternalContentFunction func);

    bool FindExternalContent(ExternalContentInfo *out, ContentId content_id, ExternalContentQuery query);
    Result ReadExternalContent(void *dst, size_t size, ContentId content_id, s64 offset);
    bool ForgetExternalContent(ContentId content_id);

}
