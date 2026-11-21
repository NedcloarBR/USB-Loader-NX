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
#include "usbl_content_index.hpp"
#include "../storage/usbl_usb_file_storage.hpp"
#include "../storage/usbl_volume.hpp"
#include "../usbl_results.hpp"

namespace ams::mitm::usbl {

    namespace {

        constexpr s32 MaxXciCount     = 64;
        constexpr s32 MaxContentCount = 512;
        /* "/usbloadernx/games/<folder>/<file>": two long names plus the fixed prefix. */
        constexpr size_t MaxPathLength = 0x300;

        struct Container {
            char path[MaxPathLength];
            s32 drive;
            bool used;
            bool has_ticket;
            ContainerTicket ticket;
            s32 fragments;
        };

        constinit Container g_containers[MaxXciCount] = {};

        constinit IndexedContent g_contents[MaxContentCount] = {};
        constinit s32 g_content_count = 0;

        /* Files left (wholly or partly) out of the index because it was full, since TakeIndexFullCount. */
        constinit s32 g_index_full_files = 0;
        constinit s32 g_last_full_container = -1;

        void AssertLocked() {
            AMS_ASSERT(GetVolumeLock().IsLockedByCurrentThread());
        }

        template<typename Predicate>
        Result FindIndexedContentIf(IndexedContent *out, const char **out_xci_path, Predicate predicate) {
            AssertLocked();

            for (s32 i = 0; i < g_content_count; ++i) {
                if (predicate(g_contents[i])) {
                    *out          = g_contents[i];
                    *out_xci_path = g_containers[g_contents[i].xci_index].path;
                    R_SUCCEED();
                }
            }

            R_THROW(usbl::ResultContentNotFound());
        }

        Result OpenContentStorage(std::shared_ptr<fs::IStorage> *out, const IndexedContent &content, const char *xci_path) {
            auto storage = fs::AllocateShared<UsbFileStorage>();
            R_UNLESS(storage != nullptr, fs::ResultAllocationMemoryFailedNew());
            R_TRY(storage->Initialize(content.drive, xci_path, content.offset, content.size));

            *out = std::move(storage);
            R_SUCCEED();
        }

    }

    void ClearContentIndex(s32 drive) {
        AssertLocked();

        g_content_count = static_cast<s32>(std::remove_if(g_contents, g_contents + g_content_count, [drive](const IndexedContent &content) { return content.drive == drive; }) - g_contents);

        for (auto &container : g_containers) {
            if (container.used && container.drive == drive) {
                container.used = false;
            }
        }
    }

    Result AddIndexedXci(s32 *out_index, s32 drive, const char *path) {
        AssertLocked();

        for (s32 i = 0; i < MaxXciCount; ++i) {
            if (!g_containers[i].used) {
                util::Strlcpy(g_containers[i].path, path, static_cast<int>(MaxPathLength));
                g_containers[i].drive      = drive;
                g_containers[i].used       = true;
                g_containers[i].has_ticket = false;
                g_containers[i].fragments  = 1;
                *out_index = i;
                R_SUCCEED();
            }
        }

        ++g_index_full_files;
        R_THROW(usbl::ResultIndexFull());
    }

    Result AddIndexedContent(const IndexedContent &content) {
        AssertLocked();
        AMS_ASSERT(0 <= content.xci_index && content.xci_index < MaxXciCount);
        AMS_ASSERT(g_containers[content.xci_index].used && g_containers[content.xci_index].drive == content.drive);
        if (g_content_count >= MaxContentCount) {
            if (g_last_full_container != content.xci_index) {
                g_last_full_container = content.xci_index;
                ++g_index_full_files;
            }
            R_THROW(usbl::ResultIndexFull());
        }

        g_contents[g_content_count++] = content;
        R_SUCCEED();
    }

    void MarkIndexedPatchContent(ncm::ContentId content_id) {
        AssertLocked();

        for (s32 i = 0; i < g_content_count; ++i) {
            if (g_contents[i].content_id == content_id) {
                g_contents[i].is_patch = true;
            }
        }
    }

    Result SetContainerTicket(s32 xci_index, const ContainerTicket &ticket) {
        AssertLocked();
        R_UNLESS(0 <= xci_index && xci_index < MaxXciCount && g_containers[xci_index].used, usbl::ResultOutOfRange());

        g_containers[xci_index].ticket     = ticket;
        g_containers[xci_index].has_ticket = true;
        R_SUCCEED();
    }

    Result SetContainerFragments(s32 xci_index, s32 fragments) {
        AssertLocked();
        R_UNLESS(0 <= xci_index && xci_index < MaxXciCount && g_containers[xci_index].used, usbl::ResultOutOfRange());

        g_containers[xci_index].fragments = fragments;
        R_SUCCEED();
    }

    Result GetContainerInfo(ContainerInfo *out, s32 xci_index) {
        AssertLocked();
        R_UNLESS(0 <= xci_index && xci_index < MaxXciCount && g_containers[xci_index].used, usbl::ResultOutOfRange());

        *out = { .path = g_containers[xci_index].path, .fragments = g_containers[xci_index].fragments };
        R_SUCCEED();
    }

    Result ReadContentTicket(TicketData *out, ncm::ContentId content_id) {
        std::scoped_lock lk(GetVolumeLock());

        IndexedContent content;
        const char *path;
        R_TRY(FindIndexedContent(std::addressof(content), std::addressof(path), content_id));

        const Container &container = g_containers[content.xci_index];
        R_UNLESS(container.has_ticket, usbl::ResultContentNotFound());
        R_UNLESS(0 < container.ticket.ticket_size && container.ticket.ticket_size <= static_cast<s64>(sizeof(out->ticket)), usbl::ResultOutOfRange());
        R_UNLESS(0 < container.ticket.cert_size   && container.ticket.cert_size   <= static_cast<s64>(sizeof(out->cert)),   usbl::ResultOutOfRange());

        FIL file;
        R_TRY(ConvertFatFsResult(f_open(std::addressof(file), path, FA_READ)));
        ON_SCOPE_EXIT { f_close(std::addressof(file)); };

        R_TRY(ReadFileAt(std::addressof(file), container.ticket.ticket_offset, out->ticket, container.ticket.ticket_size));
        R_TRY(ReadFileAt(std::addressof(file), container.ticket.cert_offset,   out->cert,   container.ticket.cert_size));
        out->ticket_size = container.ticket.ticket_size;
        out->cert_size   = container.ticket.cert_size;
        R_SUCCEED();
    }

    void RemoveIndexedProgram(ncm::ProgramId program_id) {
        AssertLocked();

        const ncm::ProgramId patch_id = GetPatchId(program_id);
        g_content_count = static_cast<s32>(std::remove_if(g_contents, g_contents + g_content_count, [program_id, patch_id](const IndexedContent &content) {
            return content.program_id == program_id || content.program_id == patch_id;
        }) - g_contents);
    }

    void RemoveIndexedPatch(ncm::ProgramId program_id) {
        AssertLocked();

        const ncm::ProgramId patch_id = GetPatchId(program_id);
        g_content_count = static_cast<s32>(std::remove_if(g_contents, g_contents + g_content_count, [program_id, patch_id](const IndexedContent &content) {
            return content.is_patch && (content.program_id == program_id || content.program_id == patch_id);
        }) - g_contents);
    }

    s32 GetIndexedApplications(ncm::ProgramId *out, s32 max_count, s32 drive) {
        AssertLocked();

        s32 count = 0;
        for (s32 i = 0; i < g_content_count && count < max_count; ++i) {
            const IndexedContent &content = g_contents[i];
            if (content.content_type != NcaContentType::Program || (drive >= 0 && content.drive != drive) || std::find(out, out + count, content.program_id) != out + count) {
                continue;
            }
            out[count++] = content.program_id;
        }
        return count;
    }

    s32 TakeIndexFullCount() {
        AssertLocked();
        const s32 count = g_index_full_files;
        g_index_full_files = 0;
        g_last_full_container = -1;
        return count;
    }

    s32 GetIndexedContentCount() {
        AssertLocked();
        return g_content_count;
    }

    Result GetIndexedContent(IndexedContent *out, s32 index) {
        AssertLocked();
        R_UNLESS(0 <= index && index < g_content_count, usbl::ResultOutOfRange());

        *out = g_contents[index];
        R_SUCCEED();
    }

    bool IsIndexedProgram(ncm::ProgramId program_id) {
        AssertLocked();
        return std::any_of(g_contents, g_contents + g_content_count, [program_id](const IndexedContent &content) {
            return content.program_id == program_id;
        });
    }

    Result FindIndexedContent(IndexedContent *out, const char **out_xci_path, ncm::ProgramId program_id, NcaContentType content_type, ContentLayer layer) {
        const auto matches = [&](const IndexedContent &content, bool is_patch) {
            return content.program_id == program_id && content.content_type == content_type && content.is_patch == is_patch;
        };

        const Result base = FindIndexedContentIf(out, out_xci_path, [&](const IndexedContent &content) { return matches(content, false); });
        if (layer == ContentLayer::Current) {
            /* An update is only used from the game's own drive; alone (no base game indexed) it is used as it is. */
            IndexedContent patch;
            const char *patch_path;
            if (R_SUCCEEDED(FindIndexedContentIf(std::addressof(patch), std::addressof(patch_path), [&](const IndexedContent &content) { return matches(content, true); })) &&
                (R_FAILED(base) || patch.drive == out->drive)) {
                *out = patch;
                *out_xci_path = patch_path;
                R_SUCCEED();
            }
        }
        R_RETURN(base);
    }

    Result FindIndexedContent(IndexedContent *out, const char **out_xci_path, ncm::ContentId content_id) {
        R_RETURN(FindIndexedContentIf(out, out_xci_path, [&](const IndexedContent &content) {
            return content.content_id == content_id;
        }));
    }

    Result OpenIndexedContentStorage(std::shared_ptr<fs::IStorage> *out, ncm::ProgramId program_id, NcaContentType content_type, ContentLayer layer) {
        std::scoped_lock lk(GetVolumeLock());

        IndexedContent content;
        const char *xci_path;
        R_TRY(FindIndexedContent(std::addressof(content), std::addressof(xci_path), program_id, content_type, layer));
        R_RETURN(OpenContentStorage(out, content, xci_path));
    }

    Result OpenIndexedContentStorage(std::shared_ptr<fs::IStorage> *out, ncm::ContentId content_id) {
        std::scoped_lock lk(GetVolumeLock());

        IndexedContent content;
        const char *xci_path;
        R_TRY(FindIndexedContent(std::addressof(content), std::addressof(xci_path), content_id));
        R_RETURN(OpenContentStorage(out, content, xci_path));
    }

}
