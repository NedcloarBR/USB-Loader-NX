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
#include <usbl/paths.hpp>
#include "usbl_known_contents.hpp"
#include "usbl_content_index.hpp"
#include "../storage/usbl_volume.hpp"
#include "../usbl_log.hpp"
#include "../usbl_results.hpp"
#include "../usbl_growable_array.hpp"

namespace ams::mitm::usbl {

    namespace {

        constexpr u32 FileMagic   = util::FourCC<'U','S','L','K'>::Code;
        constexpr u32 FileVersion = ::usbl::formats::KnownContents;

        struct FileHeader {
            u32 magic;
            u32 version;
            u32 count;
            u32 reserved;
        };
        static_assert(sizeof(FileHeader) == 0x10);

        constinit os::SdkMutex g_lock;
        /* Every content of every game indexed so far: no fixed limit (FORMATS.md). */
        constinit GrowableArray<KnownContent> g_known;
        /* A newer version's file: its entries may not be laid out like ours, so it is neither read nor rewritten. */
        constinit bool g_file_is_newer = false;

        /* A copy of the index taken under the volume lock, so it is not held while the known list is changed and saved. */
        class IndexSnapshot {
            private:
                std::unique_ptr<IndexedContent[]> m_items;
                s32 m_count = 0;
            public:
                /* False when there was no memory for the copy. */
                bool Take() {
                    std::scoped_lock lk(GetVolumeLock());
                    const s32 total = GetIndexedContentCount();
                    if (total == 0) {
                        return true;
                    }
                    if (!HasHeapRoomFor(total * sizeof(IndexedContent))) {
                        return false;
                    }
                    m_items.reset(new (std::nothrow) IndexedContent[total]);
                    if (m_items == nullptr) {
                        return false;
                    }
                    for (s32 i = 0; i < total; ++i) {
                        if (R_SUCCEEDED(GetIndexedContent(m_items.get() + m_count, i))) {
                            ++m_count;
                        }
                    }
                    return true;
                }

                const IndexedContent *Get() const { return m_items.get(); }
                s32 Count() const { return m_count; }
        };

        void FormatFilePath(char *dst, size_t dst_size) {
            util::SNPrintf(dst, dst_size, "%s:%s", ams::fs::impl::SdCardFileSystemMountName, ::usbl::paths::KnownContents);
        }

        KnownContent *FindLocked(ncm::ContentId content_id) {
            for (auto &known : g_known) {
                if (known.content_id == content_id) {
                    return &known;
                }
            }
            return nullptr;
        }

        Result SaveLocked() {
            R_UNLESS(!g_file_is_newer, usbl::ResultNewerFormat());

            char directory[ams::fs::EntryNameLengthMax + 1];
            for (const char *dir : { ::usbl::paths::Root, ::usbl::paths::DataDir }) {
                util::SNPrintf(directory, sizeof(directory), "%s:%s", ams::fs::impl::SdCardFileSystemMountName, dir);
                R_DISCARD(ams::fs::CreateDirectory(directory));
            }

            char path[ams::fs::EntryNameLengthMax + 1];
            FormatFilePath(path, sizeof(path));

            const FileHeader header = { .magic = FileMagic, .version = FileVersion, .count = static_cast<u32>(g_known.size()), .reserved = 0 };
            const size_t size = sizeof(header) + g_known.size() * sizeof(KnownContent);

            R_DISCARD(ams::fs::DeleteFile(path));
            R_TRY(ams::fs::CreateFile(path, size));

            ams::fs::FileHandle file;
            R_TRY(ams::fs::OpenFile(std::addressof(file), path, ams::fs::OpenMode_Write));
            ON_SCOPE_EXIT { ams::fs::CloseFile(file); };

            R_TRY(ams::fs::WriteFile(file, 0, std::addressof(header), sizeof(header), ams::fs::WriteOption::None));
            R_RETURN(ams::fs::WriteFile(file, sizeof(header), g_known.begin(), g_known.size() * sizeof(KnownContent), ams::fs::WriteOption::Flush));
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
        g_known.Clear();
        g_file_is_newer = false;

        char path[ams::fs::EntryNameLengthMax + 1];
        FormatFilePath(path, sizeof(path));

        ams::fs::FileHandle file;
        if (R_FAILED(ams::fs::OpenFile(std::addressof(file), path, ams::fs::OpenMode_Read))) {
            Log("known contents: none yet");
            return;
        }
        ON_SCOPE_EXIT { ams::fs::CloseFile(file); };

        FileHeader header;
        if (R_SUCCEEDED(ams::fs::ReadFile(file, 0, std::addressof(header), sizeof(header))) && header.magic == FileMagic && header.version > FileVersion) {
            Log("known contents: file from a newer version (format %u), left untouched", header.version);
            g_file_is_newer = true;
            return;
        }
        if (header.magic != FileMagic || header.version != FileVersion) {
            Log("known contents: file invalid, ignored");
            return;
        }

        if (!g_known.Reserve(header.count)) {
            Log("known contents: no memory for %u contents, ignored", header.count);
            return;
        }
        g_known.SetCount(header.count);
        if (header.count > 0 && R_FAILED(ams::fs::ReadFile(file, sizeof(header), g_known.begin(), header.count * sizeof(KnownContent)))) {
            Log("known contents: file truncated, ignored");
            g_known.Clear();
            return;
        }
        Log("known contents: %zu loaded", g_known.size());
    }

    bool IsKnownContentsFromNewerVersion() {
        std::scoped_lock lk(g_lock);
        return g_file_is_newer;
    }

    void RememberIndexedContents() {
        AMS_ASSERT(!GetVolumeLock().IsLockedByCurrentThread());

        IndexSnapshot snapshot;
        if (!snapshot.Take()) {
            Log("known contents: no memory to remember the indexed contents");
            return;
        }
        const IndexedContent *indexed = snapshot.Get();
        const s32 indexed_count = snapshot.Count();

        std::scoped_lock lk(g_lock);

        bool changed = false;
        for (s32 i = 0; i < indexed_count; ++i) {
            const KnownContent known = MakeKnownContent(indexed[i]);
            if (KnownContent *existing = FindLocked(known.content_id); existing != nullptr) {
                if (std::memcmp(existing, std::addressof(known), sizeof(known)) != 0) {
                    *existing = known;
                    changed = true;
                }
            } else if (g_known.PushBack(known)) {
                changed = true;
            } else {
                Log("known contents: no memory left, %s not remembered", ncm::GetContentIdString(known.content_id).data);
            }
        }

        if (!changed) {
            return;
        }

        if (const Result result = SaveLocked(); usbl::ResultNewerFormat::Includes(result)) {
            /* Logged once at load. */
        } else if (R_FAILED(result)) {
            Log("known contents: save failed 2%03d-%04d", result.GetModule(), result.GetDescription());
        } else {
            Log("known contents: %zu saved", g_known.size());
        }
    }

    namespace {

        ncm::ProgramId GetApplicationOf(const KnownContent &known) {
            const u64 id = IsAddOnContentTitleId(known.program_id) ? known.program_id.value - 0x1000 : known.program_id.value;
            return { id & ~TitleIdKindMask };
        }

        bool IsIndexed(const IndexedContent *indexed, s32 count, ncm::ContentId content_id) {
            return std::any_of(indexed, indexed + count, [content_id](const IndexedContent &content) { return content.content_id == content_id; });
        }

        bool IsGameIndexed(const IndexedContent *indexed, s32 count, ncm::ProgramId application_id) {
            return std::any_of(indexed, indexed + count, [application_id](const IndexedContent &content) {
                return content.program_id == application_id && content.content_type == NcaContentType::Meta && !content.is_patch;
            });
        }

    }

    void ForgetDeletedGameParts() {
        AMS_ASSERT(!GetVolumeLock().IsLockedByCurrentThread());

        IndexSnapshot snapshot;
        if (!snapshot.Take()) {
            return;
        }
        const IndexedContent *indexed = snapshot.Get();
        const s32 indexed_count = snapshot.Count();

        std::scoped_lock lk(g_lock);

        const size_t old_count = g_known.size();
        g_known.RemoveIf([&](const KnownContent &known) {
            const bool is_part = known.is_patch || IsAddOnContentTitleId(known.program_id);
            if (!is_part || IsIndexed(indexed, indexed_count, known.content_id) || !IsGameIndexed(indexed, indexed_count, GetApplicationOf(known))) {
                return false;
            }
            Log("known contents: %016" PRIx64 " %s content deleted from its game's drive, forgotten", known.program_id.value, known.is_patch ? "update" : "add-on");
            return true;
        });

        if (g_known.size() == old_count) {
            return;
        }
        if (const Result result = SaveLocked(); usbl::ResultNewerFormat::Includes(result)) {
            /* Logged once at load. */
        } else if (R_FAILED(result)) {
            Log("known contents: save failed 2%03d-%04d", result.GetModule(), result.GetDescription());
        }
    }

    namespace {

        template<typename Predicate>
        void ForgetKnownIf(Predicate predicate) {
            std::scoped_lock lk(g_lock);

            const size_t old_count = g_known.size();
            g_known.RemoveIf(predicate);
            if (g_known.size() == old_count) {
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

        if (FindLocked(content_id) == nullptr) {
            return false;
        }
        g_known.RemoveIf([content_id](const KnownContent &known) { return known.content_id == content_id; });

        if (const Result result = SaveLocked(); usbl::ResultNewerFormat::Includes(result)) {
            /* Logged once at load. */
        } else if (R_FAILED(result)) {
            Log("known contents: save failed 2%03d-%04d", result.GetModule(), result.GetDescription());
        }
        return true;
    }

    s32 GetKnownApplications(ncm::ProgramId *out, s32 max_count) {
        std::scoped_lock lk(g_lock);

        s32 count = 0;
        for (size_t i = 0; i < g_known.size() && count < max_count; ++i) {
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
            for (size_t i = 0; i < g_known.size(); ++i) {
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
        for (size_t i = 0; i < g_known.size(); ++i) {
            const u64 id = g_known[i].program_id.value;
            if (id == application_id.value || id == GetPatchId(application_id).value || (id & ~AddOnContentIndexMask) == add_on_base) {
                size += g_known[i].size;
            }
        }
        return size;
    }

    bool IsKnownPatchedProgram(ncm::ProgramId program_id) {
        std::scoped_lock lk(g_lock);
        return std::any_of(g_known.begin(), g_known.end(), [program_id](const KnownContent &known) {
            return known.program_id == program_id && known.is_patch && known.content_type == static_cast<u8>(NcaContentType::Program);
        });
    }

    bool IsKnownProgram(ncm::ProgramId program_id) {
        std::scoped_lock lk(g_lock);
        return std::any_of(g_known.begin(), g_known.end(), [program_id](const KnownContent &known) { return known.program_id == program_id; });
    }

}
