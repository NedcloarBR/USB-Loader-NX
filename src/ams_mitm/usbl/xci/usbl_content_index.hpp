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

namespace ams::mitm::usbl {

    using NcaContentType = fssystem::NcaHeader::ContentType;

    /* Title ids: an application ends in 000 and its update is the same id with 800. */
    constexpr u64 TitleIdKindMask = 0x1FFF;
    constexpr u64 PatchIdKind     = 0x800;

    constexpr bool IsApplicationTitleId(ncm::ProgramId id) { return (id.value & TitleIdKindMask) == 0; }
    constexpr bool IsPatchId(ncm::ProgramId id) { return (id.value & TitleIdKindMask) == PatchIdKind; }
    /* Add-on content ids follow their application's: application id + 0x1000 + index. */
    constexpr bool IsAddOnContentTitleId(ncm::ProgramId id) { return !IsApplicationTitleId(id) && !IsPatchId(id); }
    constexpr ncm::ProgramId GetPatchId(ncm::ProgramId application_id) { return { application_id.value | PatchIdKind }; }
    constexpr ncm::ProgramId GetApplicationIdOfPatch(ncm::ProgramId patch_id) { return { patch_id.value & ~PatchIdKind }; }

    /* An update's NCAs carry the application's program id: Current prefers the update's content, Base never returns it. */
    enum class ContentLayer {
        Base,
        Current,
    };

    struct IndexedContent {
        ncm::ProgramId program_id;
        NcaContentType content_type;
        ncm::ContentId content_id;
        fs::RightsId rights_id;
        u8 key_generation;
        ContentSource source;
        bool is_patch;
        s32 drive;
        s32 xci_index;
        s64 offset;
        s64 size;
    };

    /* Only NCAs that passed hash and signature validation are ever added. All functions below require the volume lock. */

    /* Forgets every container and content of one drive; the other drives' entries stay. */
    void ClearContentIndex(s32 drive);
    /* XCI and NSP files share this container table; xci_index refers to either. path includes the volume prefix. */
    Result AddIndexedXci(s32 *out_index, s32 drive, const char *path);
    Result AddIndexedContent(const IndexedContent &content);
    /* Flags a content as belonging to an update (listed in its meta); unknown ids are ignored. */
    void MarkIndexedPatchContent(ncm::ContentId content_id);

    /* NSPs carry the ticket (.tik) and certificate chain (.cert) eShop content needs. */
    struct ContainerTicket {
        s64 ticket_offset;
        s64 ticket_size;
        s64 cert_offset;
        s64 cert_size;
    };

    Result SetContainerTicket(s32 xci_index, const ContainerTicket &ticket);

    /* Pieces the container file is split in on the drive (1 = contiguous), or negative when too fragmented to map. */
    Result SetContainerFragments(s32 xci_index, s32 fragments);

    struct ContainerInfo {
        const char *path;
        s32 fragments;
    };

    Result GetContainerInfo(ContainerInfo *out, s32 xci_index);

    struct TicketData {
        u8 ticket[0x400];
        size_t ticket_size;
        u8 cert[0x1000];
        size_t cert_size;
    };

    /* Reads the ticket shipped in the container of an indexed content. Takes the volume lock itself. */
    Result ReadContentTicket(TicketData *out, ncm::ContentId content_id);

    /* Drops every content of one application and of its update (a copy installed on the console or SD card takes precedence). */
    void RemoveIndexedProgram(ncm::ProgramId program_id);
    /* Drops only the application's update (it cannot be registered, so the base game must not get its code or RomFS). */
    void RemoveIndexedPatch(ncm::ProgramId program_id);

    /* Applications with a Program NCA indexed (on one drive, or on any when drive is negative). */
    s32 GetIndexedApplications(ncm::ProgramId *out, s32 max_count, s32 drive);

    s32 GetIndexedContentCount();
    /* Files that did not fit in the index (no memory left for it) since the last call, which resets it. */
    s32 TakeIndexFullCount();
    Result GetIndexedContent(IndexedContent *out, s32 index);
    bool IsIndexedProgram(ncm::ProgramId program_id);
    Result FindIndexedContent(IndexedContent *out, const char **out_xci_path, ncm::ProgramId program_id, NcaContentType content_type, ContentLayer layer);
    Result FindIndexedContent(IndexedContent *out, const char **out_xci_path, ncm::ContentId content_id);

    /* Takes the volume lock itself. Returns the raw (still encrypted) NCA bytes. */
    Result OpenIndexedContentStorage(std::shared_ptr<fs::IStorage> *out, ncm::ProgramId program_id, NcaContentType content_type, ContentLayer layer);
    Result OpenIndexedContentStorage(std::shared_ptr<fs::IStorage> *out, ncm::ContentId content_id);

}
