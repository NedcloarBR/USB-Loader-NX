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
#include "usbl_game_indexer.hpp"
#include "usbl_content_index.hpp"
#include "usbl_content_meta.hpp"
#include "usbl_xci_reader.hpp"
#include "../nca/usbl_nca_filesystem.hpp"
#include "../storage/usbl_usb_file_storage.hpp"
#include "../storage/usbl_volume.hpp"
#include "../usbl_log.hpp"
#include "../usbl_results.hpp"

namespace ams::mitm::usbl::content {

    namespace {

        constexpr const char SecurePartitionName[] = "secure";
        constexpr const char NcaExtension[]         = ".nca";
        constexpr const char MetaExtension[]        = ".cnmt.nca";
        constexpr const char TicketExtension[]      = ".tik";
        constexpr const char CertificateExtension[] = ".cert";

        struct Summary {
            s32 nca_count;
            s32 indexed_count;
            s32 hash_failure_count;
        };

        const char *GetContentTypeName(NcaContentType type) {
            switch (type) {
                case NcaContentType::Program:    return "Program";
                case NcaContentType::Meta:       return "Meta";
                case NcaContentType::Control:    return "Control";
                case NcaContentType::Manual:     return "Manual";
                case NcaContentType::Data:       return "Data";
                case NcaContentType::PublicData: return "PublicData";
                default:                         return "Unknown";
            }
        }

        bool HasExtension(const char *name, const char *ext) {
            const size_t name_len = std::strlen(name);
            const size_t ext_len  = std::strlen(ext);
            return name_len >= ext_len && std::strcmp(name + name_len - ext_len, ext) == 0;
        }

        /* Tickets are named after their rights id: "<32 hex chars>.tik". Zero when the name is something else. */
        fs::RightsId GetRightsIdFromName(const char *name) {
            fs::RightsId rights_id = {};
            if (const auto id = ncm::GetContentIdFromString(name, std::strlen(name)); id.has_value()) {
                static_assert(sizeof(*id) == sizeof(rights_id));
                std::memcpy(std::addressof(rights_id), std::addressof(*id), sizeof(rights_id));
            }
            return rights_id;
        }

        /* Each .tik with the .cert of the same name, or else the container's first .cert: one per eShop title it holds (a
         * game, its update and each DLC). Returns how many were added. */
        template<typename GetEntry>
        s32 AddContainerTickets(s32 container_index, s32 entry_count, GetEntry get_entry) {
            Hfs0Entry fallback_cert = {};
            for (s32 i = 0; i < entry_count; ++i) {
                if (get_entry(std::addressof(fallback_cert), i) && HasExtension(fallback_cert.name, CertificateExtension)) {
                    break;
                }
                fallback_cert = {};
            }
            if (fallback_cert.size <= 0) {
                return 0;
            }

            s32 added = 0;
            for (s32 i = 0; i < entry_count; ++i) {
                Hfs0Entry ticket;
                if (!get_entry(std::addressof(ticket), i) || !HasExtension(ticket.name, TicketExtension)) {
                    continue;
                }

                const size_t stem_length = std::strlen(ticket.name) - std::strlen(TicketExtension);
                Hfs0Entry cert = fallback_cert;
                for (s32 j = 0; j < entry_count; ++j) {
                    Hfs0Entry candidate;
                    if (get_entry(std::addressof(candidate), j) && HasExtension(candidate.name, CertificateExtension) &&
                        std::strlen(candidate.name) == stem_length + std::strlen(CertificateExtension) && std::strncmp(candidate.name, ticket.name, stem_length) == 0) {
                        cert = candidate;
                        break;
                    }
                }

                const ContainerTicket stored = { .rights_id = GetRightsIdFromName(ticket.name), .ticket_offset = ticket.offset, .ticket_size = ticket.size, .cert_offset = cert.offset, .cert_size = cert.size };
                if (R_SUCCEEDED(AddContainerTicket(container_index, stored))) {
                    ++added;
                }
            }
            return added;
        }

        Result IndexNca(s32 drive, const char *container_path, s32 container_index, const Hfs0Entry &entry, ContentSource source) {
            /* NCA entries are named after their content id: "<32 hex chars>.nca" or "<32 hex chars>.cnmt.nca". */
            const auto content_id = ncm::GetContentIdFromString(entry.name, std::strlen(entry.name));
            R_UNLESS(content_id.has_value(), usbl::ResultInvalidHfs0Header());

            auto storage = fs::AllocateShared<UsbFileStorage>();
            R_UNLESS(storage != nullptr, fs::ResultAllocationMemoryFailedNew());
            R_TRY(storage->Initialize(drive, container_path, entry.offset, entry.size));

            std::shared_ptr<fssystem::NcaReader> reader;
            bool is_resigned;
            R_TRY(nca::OpenReader(std::addressof(reader), std::addressof(is_resigned), std::move(storage)));

            fs::RightsId rights_id;
            reader->GetRightsId(rights_id.data, sizeof(rights_id.data));

            const IndexedContent content = {
                .program_id     = { reader->GetProgramId() },
                .content_type   = reader->GetContentType(),
                .content_id     = *content_id,
                .rights_id      = rights_id,
                .key_generation = reader->GetKeyGeneration(),
                .source         = source,
                .is_patch       = false,
                .drive          = drive,
                .container_index      = container_index,
                .offset         = entry.offset,
                .size           = entry.size,
            };
            R_TRY(AddIndexedContent(content));

            Log("    %s: %-7s program=%016" PRIx64 " keygen=%u dist=%s rights=%s sign1=%s size=%" PRIu64 "MB",
                entry.name,
                GetContentTypeName(content.content_type),
                content.program_id.value,
                reader->GetKeyGeneration(),
                reader->GetDistributionType() == fssystem::NcaHeader::DistributionType::GameCard ? "gamecard" : "download",
                rights_id != fs::InvalidRightsId ? "titlekey" : "none",
                is_resigned ? "resigned" : "ok",
                static_cast<u64>(content.size) / 1_MB);
            R_SUCCEED();
        }

        void IndexSecurePartition(s32 drive, const char *container_path, s32 container_index, FIL *file, const XciReader &xci) {
            Hfs0Partition secure;
            if (const Result result = xci.OpenPartition(std::addressof(secure), SecurePartitionName); R_FAILED(result)) {
                Log("  secure partition invalid: 2%03d-%04d", result.GetModule(), result.GetDescription());
                return;
            }

            Summary summary = {};
            for (s32 i = 0; i < secure.GetEntryCount(); ++i) {
                Hfs0Entry entry;
                if (R_FAILED(secure.GetEntry(std::addressof(entry), i))) {
                    continue;
                }
                if (!HasExtension(entry.name, NcaExtension)) {
                    continue;
                }

                ++summary.nca_count;
                if (R_FAILED(VerifyEntryHash(file, entry))) {
                    ++summary.hash_failure_count;
                    Log("    %s: HASH MISMATCH (file corrupted)", entry.name);
                    continue;
                }

                if (const Result result = IndexNca(drive, container_path, container_index, entry, ContentSource::Xci); R_FAILED(result)) {
                    Log("    %s: not indexed 2%03d-%04d", entry.name, result.GetModule(), result.GetDescription());
                    continue;
                }
                ++summary.indexed_count;
            }

            /* An update merged into the XCI is eShop content: its ticket comes along. */
            const s32 tickets = AddContainerTickets(container_index, secure.GetEntryCount(), [&secure](Hfs0Entry *out, s32 index) { return R_SUCCEEDED(secure.GetEntry(out, index)); });

            Log("  secure: %d NCAs, %d indexed, %d hash failures, %d tickets", summary.nca_count, summary.indexed_count, summary.hash_failure_count, tickets);
        }

        /* An update's NCAs reuse the application's program id; only the update's own meta (id ending in 800) tells them apart. */
        void MarkPatchContents(s32 container_index) {
            for (s32 i = 0; i < GetIndexedContentCount(); ++i) {
                IndexedContent meta;
                if (R_FAILED(GetIndexedContent(std::addressof(meta), i)) || meta.container_index != container_index || meta.content_type != NcaContentType::Meta || !IsPatchId(meta.program_id)) {
                    continue;
                }

                ncm::AutoBuffer buffer;
                if (const Result result = ReadPackagedContentMeta(std::addressof(buffer), meta.program_id); R_FAILED(result)) {
                    Log("  update %016" PRIx64 ": meta unreadable 2%03d-%04d", meta.program_id.value, result.GetModule(), result.GetDescription());
                    continue;
                }

                const ncm::PackagedContentMetaReader reader(buffer.Get(), buffer.GetSize());
                MarkIndexedPatchContent(meta.content_id);
                for (size_t j = 0; j < reader.GetContentCount(); ++j) {
                    MarkIndexedPatchContent(reader.GetContentInfo(j)->GetId());
                }
                Log("  update %016" PRIx64 " v%u: %zu contents", meta.program_id.value, reader.GetKey().version, reader.GetContentCount());
            }
        }

    }

    namespace {

        s32 LogFragmentation(FIL *file) {
            const s32 fragments = CountFileFragments(file);
            if (fragments < 0) {
                Log("  heavily fragmented (fast seek unavailable)");
            } else if (fragments > 1) {
                Log("  fragmented in %d parts", fragments);
            }
            return fragments;
        }

    }

    void IndexXci(s32 drive, const char *path) {
        AMS_ASSERT(GetVolumeLock().IsLockedByCurrentThread());
        Log("indexing %s", path);

        FIL file;
        if (const FRESULT res = f_open(std::addressof(file), path, FA_READ); res != FR_OK) {
            Log("  f_open failed: %d", static_cast<int>(res));
            return;
        }
        ON_SCOPE_EXIT { f_close(std::addressof(file)); };
        const s32 fragments = LogFragmentation(std::addressof(file));

        XciReader xci;
        if (const Result result = xci.Initialize(std::addressof(file)); R_FAILED(result)) {
            Log("  invalid XCI: 2%03d-%04d", result.GetModule(), result.GetDescription());
            return;
        }
        Log("  header at 0x%" PRIx64 ", package id %016" PRIx64 ", root hash ok", xci.GetHeaderOffset(), xci.GetPackageId());

        s32 container_index;
        if (const Result result = AddIndexedContainer(std::addressof(container_index), drive, path); R_FAILED(result)) {
            Log("  not indexed: 2%03d-%04d", result.GetModule(), result.GetDescription());
            return;
        }
        R_DISCARD(SetContainerFragments(container_index, fragments));

        IndexSecurePartition(drive, path, container_index, std::addressof(file), xci);
        MarkPatchContents(container_index);
    }

    namespace {

        constexpr u32 Pfs0Magic = util::FourCC<'P','F','S','0'>::Code;
        constexpr u32 MaxPfs0EntryCount = 256;
        constexpr size_t MaxPfs0HeaderSize = 64_KB;

        struct Pfs0Header {
            u32 magic;
            u32 entry_count;
            u32 string_table_size;
            u32 reserved;
        };
        static_assert(sizeof(Pfs0Header) == 0x10);

        struct Pfs0Entry {
            s64 offset;
            s64 size;
            u32 name_offset;
            u32 reserved;
        };
        static_assert(sizeof(Pfs0Entry) == 0x18);

    }

    namespace {

        /* A meta's title id: applications end in 000, their updates in 800 and add-on content in 1xxx. */
        void CountTitle(TitleCount *count, ncm::ProgramId meta_id) {
            switch (meta_id.value & TitleIdKindMask) {
                case 0x000:       ++count->games;   break;
                case PatchIdKind: ++count->updates; break;
                default:          ++count->add_ons; break;
            }
        }

        Result ReadNcaProgramId(ncm::ProgramId *out, s32 drive, const char *path, const Hfs0Entry &entry) {
            auto storage = fs::AllocateShared<UsbFileStorage>();
            R_UNLESS(storage != nullptr, fs::ResultAllocationMemoryFailedNew());
            R_TRY(storage->Initialize(drive, path, entry.offset, entry.size));

            std::shared_ptr<fssystem::NcaReader> reader;
            R_TRY(nca::OpenReader(std::addressof(reader), std::move(storage)));

            *out = { reader->GetProgramId() };
            R_SUCCEED();
        }

    }

    bool IndexNsp(s32 drive, const char *path, TitleCount *out_count) {
        AMS_ASSERT(GetVolumeLock().IsLockedByCurrentThread());
        Log("indexing %s", path);

        FIL file;
        if (const FRESULT res = f_open(std::addressof(file), path, FA_READ); res != FR_OK) {
            Log("  f_open failed: %d", static_cast<int>(res));
            return false;
        }
        ON_SCOPE_EXIT { f_close(std::addressof(file)); };
        const s32 fragments = LogFragmentation(std::addressof(file));

        Pfs0Header header;
        if (R_FAILED(ReadFileAt(std::addressof(file), 0, std::addressof(header), sizeof(header))) || header.magic != Pfs0Magic || header.entry_count > MaxPfs0EntryCount) {
            Log("  invalid NSP header");
            return false;
        }

        const size_t table_size = header.entry_count * sizeof(Pfs0Entry) + header.string_table_size;
        if (table_size > MaxPfs0HeaderSize) {
            Log("  invalid NSP header (table 0x%zx)", table_size);
            return false;
        }

        auto table = std::unique_ptr<u8[]>(new (std::nothrow) u8[table_size + 1]);
        if (table == nullptr || R_FAILED(ReadFileAt(std::addressof(file), sizeof(header), table.get(), table_size))) {
            Log("  cannot read NSP file table");
            return false;
        }
        table[table_size] = '\x00';

        const auto *entries = reinterpret_cast<const Pfs0Entry *>(table.get());
        const char *strings = reinterpret_cast<const char *>(table.get() + header.entry_count * sizeof(Pfs0Entry));
        const s64 data_offset = sizeof(header) + table_size;
        const s64 file_size = static_cast<s64>(f_size(std::addressof(file)));

        TitleCount count = {};
        for (u32 i = 0; i < header.entry_count; ++i) {
            const Pfs0Entry &pfs_entry = entries[i];
            const char *name = pfs_entry.name_offset < header.string_table_size ? strings + pfs_entry.name_offset : "";
            if (!HasExtension(name, MetaExtension)) {
                continue;
            }

            const Hfs0Entry meta_entry = { .offset = data_offset + pfs_entry.offset, .size = pfs_entry.size, .hashed_region_size = 0, .hash = {}, .name = name };
            ncm::ProgramId meta_id = ncm::InvalidProgramId;
            if (const Result result = ReadNcaProgramId(std::addressof(meta_id), drive, path, meta_entry); R_FAILED(result)) {
                Log("  meta unreadable 2%03d-%04d, NSP skipped", result.GetModule(), result.GetDescription());
                return false;
            }
            CountTitle(std::addressof(count), meta_id);
        }
        /* Without a meta it cannot be registered; it still counts as what it was taken for before. */
        if (count.games + count.updates + count.add_ons == 0) {
            count.games = 1;
        }

        s32 container_index;
        if (const Result result = AddIndexedContainer(std::addressof(container_index), drive, path); R_FAILED(result)) {
            Log("  not indexed: 2%03d-%04d", result.GetModule(), result.GetDescription());
            return false;
        }
        R_DISCARD(SetContainerFragments(container_index, fragments));

        Summary summary = {};
        for (u32 i = 0; i < header.entry_count; ++i) {
            const Pfs0Entry &pfs_entry = entries[i];
            if (pfs_entry.name_offset >= header.string_table_size) {
                continue;
            }

            const Hfs0Entry entry = { .offset = data_offset + pfs_entry.offset, .size = pfs_entry.size, .hashed_region_size = 0, .hash = {}, .name = strings + pfs_entry.name_offset };
            if (!HasExtension(entry.name, NcaExtension)) {
                continue;
            }

            ++summary.nca_count;
            if (pfs_entry.offset < 0 || pfs_entry.size <= 0 || entry.offset + entry.size > file_size) {
                Log("    %s: entry out of the file (truncated NSP)", entry.name);
                continue;
            }

            if (const Result result = IndexNca(drive, path, container_index, entry, ContentSource::Nsp); R_FAILED(result)) {
                Log("    %s: not indexed 2%03d-%04d", entry.name, result.GetModule(), result.GetDescription());
                continue;
            }
            ++summary.indexed_count;
        }

        const s32 tickets = AddContainerTickets(container_index, static_cast<s32>(header.entry_count), [&](Hfs0Entry *out, s32 index) {
            const Pfs0Entry &pfs_entry = entries[index];
            if (pfs_entry.name_offset >= header.string_table_size || pfs_entry.offset < 0 || pfs_entry.size <= 0 || data_offset + pfs_entry.offset + pfs_entry.size > file_size) {
                return false;
            }
            *out = { .offset = data_offset + pfs_entry.offset, .size = pfs_entry.size, .hashed_region_size = 0, .hash = {}, .name = strings + pfs_entry.name_offset };
            return true;
        });

        Log("  nsp: %d NCAs, %d indexed, %d tickets; %d games, %d updates, %d add-ons", summary.nca_count, summary.indexed_count, tickets, count.games, count.updates, count.add_ons);
        MarkPatchContents(container_index);

        *out_count = count;
        return true;
    }

    void SelfTestContentIndex() {
        std::scoped_lock lk(GetVolumeLock());

        const s32 count = GetIndexedContentCount();
        s32 ok_count = 0;
        for (s32 i = 0; i < count; ++i) {
            IndexedContent content;
            if (R_FAILED(GetIndexedContent(std::addressof(content), i))) {
                continue;
            }

            std::shared_ptr<fs::IStorage> storage;
            std::shared_ptr<fssystem::NcaReader> reader;
            Result result = OpenIndexedContentStorage(std::addressof(storage), content.content_id);
            if (R_SUCCEEDED(result)) {
                result = nca::OpenReader(std::addressof(reader), std::move(storage));
            }

            if (R_SUCCEEDED(result) && reader->GetProgramId() == content.program_id.value) {
                ++ok_count;
            } else {
                Log("  self-test %016" PRIx64 " %s failed: 2%03d-%04d", content.program_id.value, GetContentTypeName(content.content_type), result.GetModule(), result.GetDescription());
            }
        }

        Log("self-test: %d/%d indexed contents reopened and verified", ok_count, count);
    }

}
