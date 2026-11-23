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
#include "../usbl_growable_array.hpp"
#include "../nca/usbl_ncz_storage.hpp"

namespace ams::mitm::usbl {

    namespace {

        /* One per XCI or NSP indexed; a slot freed by an unplugged drive is reused, so indexes held by contents stay valid. */
        struct Container {
            /* Allocated to the path's length; stays at the same address while the list grows. */
            std::unique_ptr<char[]> path;
            s32 drive = 0;
            bool used = false;
            s32 fragments = 1;
        };

        struct StoredTicket {
            s32 drive;
            s32 container_index;
            ContainerTicket ticket;
        };

        /* As many as the drives hold (and the heap allows): no fixed limit. */
        constinit GrowableArray<Container> g_containers;
        constinit GrowableArray<IndexedContent> g_contents;
        constinit GrowableArray<StoredTicket> g_tickets;

        /* Files left (wholly or partly) out of the index because it was full, since TakeIndexFullCount. */
        constinit s32 g_index_full_files = 0;
        constinit s32 g_last_full_container = -1;

        void AssertLocked() {
            AMS_ASSERT(GetVolumeLock().IsLockedByCurrentThread());
        }

        bool IsValidContainer(s32 container_index) {
            return 0 <= container_index && static_cast<size_t>(container_index) < g_containers.size() && g_containers[container_index].used;
        }

        template<typename Predicate>
        Result FindIndexedContentIf(IndexedContent *out, const char **out_container_path, Predicate predicate) {
            AssertLocked();

            for (const auto &content : g_contents) {
                if (predicate(content)) {
                    *out          = content;
                    *out_container_path = g_containers[content.container_index].path.get();
                    R_SUCCEED();
                }
            }

            R_THROW(usbl::ResultContentNotFound());
        }

        Result OpenContentStorage(std::shared_ptr<fs::IStorage> *out, const IndexedContent &content, const char *container_path) {
            auto storage = fs::AllocateShared<UsbFileStorage>();
            R_UNLESS(storage != nullptr, fs::ResultAllocationMemoryFailedNew());
            R_TRY(storage->Initialize(content.drive, container_path, content.offset, content.stored_size));
            if (!content.is_compressed) {
                *out = std::move(storage);
                R_SUCCEED();
            }

            auto ncz = fs::AllocateShared<nca::NczStorage>();
            R_UNLESS(ncz != nullptr, fs::ResultAllocationMemoryFailedNew());
            R_TRY(ncz->Initialize(std::move(storage)));
            *out = std::move(ncz);
            R_SUCCEED();
        }

    }

    void ClearContentIndex(s32 drive) {
        AssertLocked();

        g_contents.RemoveIf([drive](const IndexedContent &content) { return content.drive == drive; });
        g_tickets.RemoveIf([drive](const StoredTicket &stored) { return stored.drive == drive; });

        bool any_used = false;
        for (auto &container : g_containers) {
            if (container.used && container.drive == drive) {
                container.used = false;
                container.path.reset();
            }
            any_used = any_used || container.used;
        }

        /* No drive left: the memory goes back to the heap. */
        if (!any_used) {
            g_containers.Release();
            g_contents.Release();
            g_tickets.Release();
        }
    }

    Result AddIndexedContainer(s32 *out_index, s32 drive, const char *path) {
        AssertLocked();

        const size_t path_size = std::strlen(path) + 1;
        std::unique_ptr<char[]> path_copy(HasHeapRoomFor(path_size) ? new (std::nothrow) char[path_size] : nullptr);
        if (path_copy == nullptr) {
            ++g_index_full_files;
            R_THROW(usbl::ResultIndexFull());
        }
        std::memcpy(path_copy.get(), path, path_size);

        s32 index = -1;
        for (size_t i = 0; i < g_containers.size(); ++i) {
            if (!g_containers[i].used) {
                index = static_cast<s32>(i);
                break;
            }
        }
        if (index < 0) {
            if (!g_containers.PushBack({})) {
                ++g_index_full_files;
                R_THROW(usbl::ResultIndexFull());
            }
            index = static_cast<s32>(g_containers.size() - 1);
        }

        Container &container = g_containers[index];
        container = { .path = std::move(path_copy), .drive = drive, .used = true, .fragments = 1 };
        *out_index = index;
        R_SUCCEED();
    }

    Result AddIndexedContent(const IndexedContent &content) {
        AssertLocked();
        AMS_ASSERT(IsValidContainer(content.container_index));
        AMS_ASSERT(g_containers[content.container_index].drive == content.drive);
        if (!g_contents.PushBack(content)) {
            if (g_last_full_container != content.container_index) {
                g_last_full_container = content.container_index;
                ++g_index_full_files;
            }
            R_THROW(usbl::ResultIndexFull());
        }
        R_SUCCEED();
    }

    void MarkIndexedPatchContent(ncm::ContentId content_id) {
        AssertLocked();

        for (auto &content : g_contents) {
            if (content.content_id == content_id) {
                content.is_patch = true;
            }
        }
    }

    Result AddContainerTicket(s32 container_index, const ContainerTicket &ticket) {
        AssertLocked();
        R_UNLESS(IsValidContainer(container_index), usbl::ResultOutOfRange());

        R_UNLESS(g_tickets.PushBack({ .drive = g_containers[container_index].drive, .container_index = container_index, .ticket = ticket }), usbl::ResultIndexFull());
        R_SUCCEED();
    }

    Result SetContainerFragments(s32 container_index, s32 fragments) {
        AssertLocked();
        R_UNLESS(IsValidContainer(container_index), usbl::ResultOutOfRange());

        g_containers[container_index].fragments = fragments;
        R_SUCCEED();
    }

    Result GetContainerInfo(ContainerInfo *out, s32 container_index) {
        AssertLocked();
        R_UNLESS(IsValidContainer(container_index), usbl::ResultOutOfRange());

        *out = { .path = g_containers[container_index].path.get(), .fragments = g_containers[container_index].fragments };
        R_SUCCEED();
    }

    Result ReadContentTicket(TicketData *out, ncm::ContentId content_id) {
        std::scoped_lock lk(GetVolumeLock());

        IndexedContent content;
        const char *path;
        R_TRY(FindIndexedContent(std::addressof(content), std::addressof(path), content_id));

        /* The ticket named after the content's rights id; a container with a single ticket is taken whatever its name. */
        const ContainerTicket *match = nullptr;
        s32 in_container = 0;
        const ContainerTicket *only = nullptr;
        for (const auto &stored : g_tickets) {
            if (stored.container_index != content.container_index) {
                continue;
            }
            ++in_container;
            only = std::addressof(stored.ticket);
            if (std::memcmp(std::addressof(stored.ticket.rights_id), std::addressof(content.rights_id), sizeof(content.rights_id)) == 0) {
                match = std::addressof(stored.ticket);
            }
        }
        if (match == nullptr && in_container == 1) {
            match = only;
        }
        R_UNLESS(match != nullptr, usbl::ResultContentNotFound());
        R_UNLESS(0 < match->ticket_size && match->ticket_size <= static_cast<s64>(sizeof(out->ticket)), usbl::ResultOutOfRange());
        R_UNLESS(0 < match->cert_size   && match->cert_size   <= static_cast<s64>(sizeof(out->cert)),   usbl::ResultOutOfRange());

        FIL file;
        R_TRY(ConvertFatFsResult(f_open(std::addressof(file), path, FA_READ)));
        ON_SCOPE_EXIT { f_close(std::addressof(file)); };

        R_TRY(ReadFileAt(std::addressof(file), match->ticket_offset, out->ticket, match->ticket_size));
        R_TRY(ReadFileAt(std::addressof(file), match->cert_offset,   out->cert,   match->cert_size));
        out->ticket_size = match->ticket_size;
        out->cert_size   = match->cert_size;
        R_SUCCEED();
    }

    void RemoveIndexedProgram(ncm::ProgramId program_id) {
        AssertLocked();

        const ncm::ProgramId patch_id = GetPatchId(program_id);
        g_contents.RemoveIf([program_id, patch_id](const IndexedContent &content) {
            return content.program_id == program_id || content.program_id == patch_id;
        });
    }

    void RemoveIndexedPatch(ncm::ProgramId program_id) {
        AssertLocked();

        const ncm::ProgramId patch_id = GetPatchId(program_id);
        g_contents.RemoveIf([program_id, patch_id](const IndexedContent &content) {
            return content.is_patch && (content.program_id == program_id || content.program_id == patch_id);
        });
    }

    s32 GetIndexedApplications(ncm::ProgramId *out, s32 max_count, s32 drive) {
        AssertLocked();

        s32 count = 0;
        for (const auto &content : g_contents) {
            if (count >= max_count) {
                break;
            }
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
        return static_cast<s32>(g_contents.size());
    }

    Result GetIndexedContent(IndexedContent *out, s32 index) {
        AssertLocked();
        R_UNLESS(0 <= index && static_cast<size_t>(index) < g_contents.size(), usbl::ResultOutOfRange());

        *out = g_contents[index];
        R_SUCCEED();
    }

    bool IsIndexedProgram(ncm::ProgramId program_id) {
        AssertLocked();
        return std::any_of(g_contents.begin(), g_contents.end(), [program_id](const IndexedContent &content) {
            return content.program_id == program_id;
        });
    }

    Result FindIndexedContent(IndexedContent *out, const char **out_container_path, ncm::ProgramId program_id, NcaContentType content_type, ContentLayer layer) {
        const auto matches = [&](const IndexedContent &content, bool is_patch) {
            return content.program_id == program_id && content.content_type == content_type && content.is_patch == is_patch;
        };

        const Result base = FindIndexedContentIf(out, out_container_path, [&](const IndexedContent &content) { return matches(content, false); });
        if (layer == ContentLayer::Current) {
            /* An update is only used from the game's own drive; alone (no base game indexed) it is used as it is. */
            IndexedContent patch;
            const char *patch_path;
            if (R_SUCCEEDED(FindIndexedContentIf(std::addressof(patch), std::addressof(patch_path), [&](const IndexedContent &content) { return matches(content, true); })) &&
                (R_FAILED(base) || patch.drive == out->drive)) {
                *out = patch;
                *out_container_path = patch_path;
                R_SUCCEED();
            }
        }
        R_RETURN(base);
    }

    Result FindIndexedContent(IndexedContent *out, const char **out_container_path, ncm::ContentId content_id) {
        R_RETURN(FindIndexedContentIf(out, out_container_path, [&](const IndexedContent &content) {
            return content.content_id == content_id;
        }));
    }

    Result OpenIndexedContentStorage(std::shared_ptr<fs::IStorage> *out, ncm::ProgramId program_id, NcaContentType content_type, ContentLayer layer) {
        std::scoped_lock lk(GetVolumeLock());

        IndexedContent content;
        const char *container_path;
        R_TRY(FindIndexedContent(std::addressof(content), std::addressof(container_path), program_id, content_type, layer));
        R_RETURN(OpenContentStorage(out, content, container_path));
    }

    Result OpenIndexedContentStorage(std::shared_ptr<fs::IStorage> *out, ncm::ContentId content_id) {
        std::scoped_lock lk(GetVolumeLock());

        IndexedContent content;
        const char *container_path;
        R_TRY(FindIndexedContent(std::addressof(content), std::addressof(container_path), content_id));
        R_RETURN(OpenContentStorage(out, content, container_path));
    }

}
