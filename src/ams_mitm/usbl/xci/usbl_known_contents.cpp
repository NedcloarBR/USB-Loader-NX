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
#include "usbl_known_contents.hpp"
#include "usbl_content_index.hpp"
#include "../storage/usbl_volume.hpp"
#include "../usbl_log.hpp"

namespace ams::mitm::usbl {

    namespace {

        constexpr u32 FileMagic   = util::FourCC<'U','S','L','K'>::Code;
        constexpr u32 FileVersion = 1;
        constexpr size_t MaxKnownContents = 512;

        struct FileHeader {
            u32 magic;
            u32 version;
            u32 count;
            u32 reserved;
        };
        static_assert(sizeof(FileHeader) == 0x10);

        constinit os::SdkMutex g_lock;
        constinit KnownContent g_known[MaxKnownContents] = {};
        constinit size_t g_known_count = 0;

        /* Only the USB volume thread remembers contents; too large for its stack. */
        constinit IndexedContent g_indexed_scratch[MaxKnownContents] = {};

        void FormatFilePath(char *dst, size_t dst_size) {
            util::SNPrintf(dst, dst_size, "%s:/atmosphere/config/usbloadernx/known_contents.bin", ams::fs::impl::SdCardFileSystemMountName);
        }

        KnownContent *FindLocked(ncm::ContentId content_id) {
            for (size_t i = 0; i < g_known_count; ++i) {
                if (g_known[i].content_id == content_id) {
                    return g_known + i;
                }
            }
            return nullptr;
        }

        Result SaveLocked() {
            char directory[ams::fs::EntryNameLengthMax + 1];
            util::SNPrintf(directory, sizeof(directory), "%s:/atmosphere/config/usbloadernx", ams::fs::impl::SdCardFileSystemMountName);
            R_DISCARD(ams::fs::CreateDirectory(directory));

            char path[ams::fs::EntryNameLengthMax + 1];
            FormatFilePath(path, sizeof(path));

            const FileHeader header = { .magic = FileMagic, .version = FileVersion, .count = static_cast<u32>(g_known_count), .reserved = 0 };
            const size_t size = sizeof(header) + g_known_count * sizeof(KnownContent);

            R_DISCARD(ams::fs::DeleteFile(path));
            R_TRY(ams::fs::CreateFile(path, size));

            ams::fs::FileHandle file;
            R_TRY(ams::fs::OpenFile(std::addressof(file), path, ams::fs::OpenMode_Write));
            ON_SCOPE_EXIT { ams::fs::CloseFile(file); };

            R_TRY(ams::fs::WriteFile(file, 0, std::addressof(header), sizeof(header), ams::fs::WriteOption::None));
            R_RETURN(ams::fs::WriteFile(file, sizeof(header), g_known, g_known_count * sizeof(KnownContent), ams::fs::WriteOption::Flush));
        }

        KnownContent MakeKnownContent(const IndexedContent &content) {
            return {
                .content_id     = content.content_id,
                .program_id     = content.program_id,
                .rights_id      = content.rights_id,
                .size           = content.size,
                .content_type   = static_cast<u8>(content.content_type),
                .key_generation = content.key_generation,
                .source         = content.source,
                .is_patch       = content.is_patch,
                .reserved       = {},
            };
        }

    }

    void LoadKnownContents() {
        std::scoped_lock lk(g_lock);
        g_known_count = 0;

        char path[ams::fs::EntryNameLengthMax + 1];
        FormatFilePath(path, sizeof(path));

        ams::fs::FileHandle file;
        if (R_FAILED(ams::fs::OpenFile(std::addressof(file), path, ams::fs::OpenMode_Read))) {
            Log("known contents: none yet");
            return;
        }
        ON_SCOPE_EXIT { ams::fs::CloseFile(file); };

        FileHeader header;
        if (R_FAILED(ams::fs::ReadFile(file, 0, std::addressof(header), sizeof(header))) || header.magic != FileMagic || header.version != FileVersion || header.count > MaxKnownContents) {
            Log("known contents: file invalid, ignored");
            return;
        }

        if (R_FAILED(ams::fs::ReadFile(file, sizeof(header), g_known, header.count * sizeof(KnownContent)))) {
            Log("known contents: file truncated, ignored");
            return;
        }

        g_known_count = header.count;
        Log("known contents: %zu loaded", g_known_count);
    }

    void RememberIndexedContents() {
        AMS_ASSERT(!GetVolumeLock().IsLockedByCurrentThread());

        IndexedContent *indexed = g_indexed_scratch;
        s32 indexed_count = 0;
        {
            std::scoped_lock lk(GetVolumeLock());
            for (s32 i = 0; i < GetIndexedContentCount() && indexed_count < static_cast<s32>(MaxKnownContents); ++i) {
                if (R_SUCCEEDED(GetIndexedContent(indexed + indexed_count, i))) {
                    ++indexed_count;
                }
            }
        }

        std::scoped_lock lk(g_lock);

        bool changed = false;
        for (s32 i = 0; i < indexed_count; ++i) {
            const KnownContent known = MakeKnownContent(indexed[i]);
            if (KnownContent *existing = FindLocked(known.content_id); existing != nullptr) {
                if (std::memcmp(existing, std::addressof(known), sizeof(known)) != 0) {
                    *existing = known;
                    changed = true;
                }
            } else if (g_known_count < MaxKnownContents) {
                g_known[g_known_count++] = known;
                changed = true;
            }
        }

        if (!changed) {
            return;
        }

        if (const Result result = SaveLocked(); R_FAILED(result)) {
            Log("known contents: save failed 2%03d-%04d", result.GetModule(), result.GetDescription());
        } else {
            Log("known contents: %zu saved", g_known_count);
        }
    }

    namespace {

        template<typename Predicate>
        void ForgetKnownIf(Predicate predicate) {
            std::scoped_lock lk(g_lock);

            const size_t old_count = g_known_count;
            g_known_count = static_cast<size_t>(std::remove_if(g_known, g_known + g_known_count, predicate) - g_known);
            if (g_known_count == old_count) {
                return;
            }

            if (const Result result = SaveLocked(); R_FAILED(result)) {
                Log("known contents: save failed 2%03d-%04d", result.GetModule(), result.GetDescription());
            }
        }

    }

    void ForgetKnownProgram(ncm::ProgramId program_id) {
        const ncm::ProgramId patch_id = GetPatchId(program_id);
        ForgetKnownIf([program_id, patch_id](const KnownContent &known) { return known.program_id == program_id || known.program_id == patch_id; });
    }

    void ForgetKnownPatch(ncm::ProgramId program_id) {
        const ncm::ProgramId patch_id = GetPatchId(program_id);
        ForgetKnownIf([program_id, patch_id](const KnownContent &known) { return known.is_patch && (known.program_id == program_id || known.program_id == patch_id); });
    }

    bool ForgetKnownContent(ncm::ContentId content_id) {
        std::scoped_lock lk(g_lock);

        const KnownContent *known = FindLocked(content_id);
        if (known == nullptr) {
            return false;
        }
        std::copy(known + 1, static_cast<const KnownContent *>(g_known + g_known_count), g_known + (known - g_known));
        --g_known_count;

        if (const Result result = SaveLocked(); R_FAILED(result)) {
            Log("known contents: save failed 2%03d-%04d", result.GetModule(), result.GetDescription());
        }
        return true;
    }

    s32 GetKnownApplications(ncm::ProgramId *out, s32 max_count) {
        std::scoped_lock lk(g_lock);

        s32 count = 0;
        for (size_t i = 0; i < g_known_count && count < max_count; ++i) {
            if (g_known[i].content_type == static_cast<u8>(NcaContentType::Meta) && IsApplicationTitleId(g_known[i].program_id)) {
                out[count++] = g_known[i].program_id;
            }
        }
        return count;
    }

    bool FindKnownContent(KnownContent *out, ncm::ContentId content_id) {
        std::scoped_lock lk(g_lock);

        const KnownContent *known = FindLocked(content_id);
        if (known == nullptr) {
            return false;
        }
        *out = *known;
        return true;
    }

    bool FindKnownProgramContent(KnownContent *out, ncm::ProgramId program_id, u8 content_type, ContentLayer layer) {
        std::scoped_lock lk(g_lock);

        const auto find = [&](bool is_patch) {
            for (size_t i = 0; i < g_known_count; ++i) {
                if (g_known[i].program_id == program_id && g_known[i].content_type == content_type && g_known[i].is_patch == is_patch) {
                    *out = g_known[i];
                    return true;
                }
            }
            return false;
        };
        return (layer == ContentLayer::Current && find(true)) || find(false);
    }

    s64 GetKnownApplicationSize(ncm::ProgramId application_id) {
        std::scoped_lock lk(g_lock);

        /* Update NCAs carry the application's program id; add-on contents are the application id + 0x1000 + index. */
        constexpr u64 AddOnContentBaseOffset = 0x1000;
        constexpr u64 AddOnContentIndexMask  = 0xFFF;
        const u64 add_on_base = application_id.value + AddOnContentBaseOffset;
        s64 size = 0;
        for (size_t i = 0; i < g_known_count; ++i) {
            const u64 id = g_known[i].program_id.value;
            if (id == application_id.value || id == GetPatchId(application_id).value || (id & ~AddOnContentIndexMask) == add_on_base) {
                size += g_known[i].size;
            }
        }
        return size;
    }

    bool IsKnownPatchedProgram(ncm::ProgramId program_id) {
        std::scoped_lock lk(g_lock);
        return std::any_of(g_known, g_known + g_known_count, [program_id](const KnownContent &known) {
            return known.program_id == program_id && known.is_patch && known.content_type == static_cast<u8>(NcaContentType::Program);
        });
    }

    bool IsKnownProgram(ncm::ProgramId program_id) {
        std::scoped_lock lk(g_lock);
        return std::any_of(g_known, g_known + g_known_count, [program_id](const KnownContent &known) { return known.program_id == program_id; });
    }

}
