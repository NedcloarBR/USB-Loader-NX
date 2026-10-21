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
#include "usbl_xci_indexer.hpp"
#include "usbl_content_index.hpp"
#include "usbl_content_meta.hpp"
#include "usbl_xci_reader.hpp"
#include "../nca/usbl_nca_filesystem.hpp"
#include "../storage/usbl_usb_file_storage.hpp"
#include "../storage/usbl_volume.hpp"
#include "../usbl_log.hpp"
#include "../usbl_results.hpp"

namespace ams::mitm::usbl::xci {

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

        Result IndexNca(s32 drive, const char *xci_path, s32 xci_index, const Hfs0Entry &entry, ContentSource source) {
            /* NCA entries are named after their content id: "<32 hex chars>.nca" or "<32 hex chars>.cnmt.nca". */
            const auto content_id = ncm::GetContentIdFromString(entry.name, std::strlen(entry.name));
            R_UNLESS(content_id.has_value(), usbl::ResultInvalidHfs0Header());

            auto storage = fs::AllocateShared<UsbFileStorage>();
            R_UNLESS(storage != nullptr, fs::ResultAllocationMemoryFailedNew());
            R_TRY(storage->Initialize(drive, xci_path, entry.offset, entry.size));

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
                .xci_index      = xci_index,
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

        void IndexSecurePartition(s32 drive, const char *xci_path, s32 xci_index, FIL *file, const XciReader &xci) {
            Hfs0Partition secure;
            if (const Result result = xci.OpenPartition(std::addressof(secure), SecurePartitionName); R_FAILED(result)) {
                Log("  secure partition invalid: 2%03d-%04d", result.GetModule(), result.GetDescription());
                return;
            }

            Summary summary = {};
            ContainerTicket ticket = {};
            for (s32 i = 0; i < secure.GetEntryCount(); ++i) {
                Hfs0Entry entry;
                if (R_FAILED(secure.GetEntry(std::addressof(entry), i))) {
                    continue;
                }

                /* An update merged into the XCI is eShop content: its ticket comes along. */
                if (HasExtension(entry.name, TicketExtension)) {
                    ticket.ticket_offset = entry.offset;
                    ticket.ticket_size   = entry.size;
                    continue;
                }
                if (HasExtension(entry.name, CertificateExtension)) {
                    ticket.cert_offset = entry.offset;
                    ticket.cert_size   = entry.size;
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

                if (const Result result = IndexNca(drive, xci_path, xci_index, entry, ContentSource::Xci); R_FAILED(result)) {
                    Log("    %s: not indexed 2%03d-%04d", entry.name, result.GetModule(), result.GetDescription());
                    continue;
                }
                ++summary.indexed_count;
            }

            if (ticket.ticket_size > 0 && ticket.cert_size > 0) {
                R_DISCARD(SetContainerTicket(xci_index, ticket));
            }

            Log("  secure: %d NCAs, %d indexed, %d hash failures%s", summary.nca_count, summary.indexed_count, summary.hash_failure_count, ticket.ticket_size > 0 ? ", ticket included" : "");
        }

        /* An update's NCAs reuse the application's program id; only the update's own meta (id ending in 800) tells them apart. */
        void MarkPatchContents(s32 xci_index) {
            for (s32 i = 0; i < GetIndexedContentCount(); ++i) {
                IndexedContent meta;
                if (R_FAILED(GetIndexedContent(std::addressof(meta), i)) || meta.xci_index != xci_index || meta.content_type != NcaContentType::Meta || !IsPatchId(meta.program_id)) {
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

        s32 xci_index;
        if (const Result result = AddIndexedXci(std::addressof(xci_index), drive, path); R_FAILED(result)) {
            Log("  not indexed: 2%03d-%04d", result.GetModule(), result.GetDescription());
            return;
        }
        R_DISCARD(SetContainerFragments(xci_index, fragments));

        IndexSecurePartition(drive, path, xci_index, std::addressof(file), xci);
        MarkPatchContents(xci_index);
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

        /* Title id of the NSP's own meta: applications end in 000, their updates in 800 and add-on content in 1xxx. */
        NspKind GetNspKind(ncm::ProgramId meta_id) {
            switch (meta_id.value & TitleIdKindMask) {
                case 0x000:       return NspKind::Application;
                case PatchIdKind: return NspKind::Patch;
                default:          return NspKind::AddOnContent;
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

    NspKind IndexNsp(s32 drive, const char *path) {
        AMS_ASSERT(GetVolumeLock().IsLockedByCurrentThread());
        Log("indexing %s", path);

        FIL file;
        if (const FRESULT res = f_open(std::addressof(file), path, FA_READ); res != FR_OK) {
            Log("  f_open failed: %d", static_cast<int>(res));
            return NspKind::Unknown;
        }
        ON_SCOPE_EXIT { f_close(std::addressof(file)); };
        const s32 fragments = LogFragmentation(std::addressof(file));

        Pfs0Header header;
        if (R_FAILED(ReadFileAt(std::addressof(file), 0, std::addressof(header), sizeof(header))) || header.magic != Pfs0Magic || header.entry_count > MaxPfs0EntryCount) {
            Log("  invalid NSP header");
            return NspKind::Unknown;
        }

        const size_t table_size = header.entry_count * sizeof(Pfs0Entry) + header.string_table_size;
        if (table_size > MaxPfs0HeaderSize) {
            Log("  invalid NSP header (table 0x%zx)", table_size);
            return NspKind::Unknown;
        }

        auto table = std::unique_ptr<u8[]>(new (std::nothrow) u8[table_size + 1]);
        if (table == nullptr || R_FAILED(ReadFileAt(std::addressof(file), sizeof(header), table.get(), table_size))) {
            Log("  cannot read NSP file table");
            return NspKind::Unknown;
        }
        table[table_size] = '\x00';

        const auto *entries = reinterpret_cast<const Pfs0Entry *>(table.get());
        const char *strings = reinterpret_cast<const char *>(table.get() + header.entry_count * sizeof(Pfs0Entry));
        const s64 data_offset = sizeof(header) + table_size;
        const s64 file_size = static_cast<s64>(f_size(std::addressof(file)));

        NspKind kind = NspKind::Application;
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
                return NspKind::Unknown;
            }
            kind = GetNspKind(meta_id);
            break;
        }

        s32 container_index;
        if (const Result result = AddIndexedXci(std::addressof(container_index), drive, path); R_FAILED(result)) {
            Log("  not indexed: 2%03d-%04d", result.GetModule(), result.GetDescription());
            return NspKind::Unknown;
        }
        R_DISCARD(SetContainerFragments(container_index, fragments));

        Summary summary = {};
        ContainerTicket ticket = {};
        for (u32 i = 0; i < header.entry_count; ++i) {
            const Pfs0Entry &pfs_entry = entries[i];
            if (pfs_entry.name_offset >= header.string_table_size) {
                continue;
            }

            const Hfs0Entry entry = { .offset = data_offset + pfs_entry.offset, .size = pfs_entry.size, .hashed_region_size = 0, .hash = {}, .name = strings + pfs_entry.name_offset };
            if (HasExtension(entry.name, TicketExtension)) {
                ticket.ticket_offset = entry.offset;
                ticket.ticket_size   = entry.size;
                continue;
            }
            if (HasExtension(entry.name, CertificateExtension)) {
                ticket.cert_offset = entry.offset;
                ticket.cert_size   = entry.size;
                continue;
            }
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

        if (ticket.ticket_size > 0 && ticket.cert_size > 0) {
            R_DISCARD(SetContainerTicket(container_index, ticket));
        }

        Log("  nsp: %d NCAs, %d indexed%s", summary.nca_count, summary.indexed_count, ticket.ticket_size > 0 ? ", ticket included" : "");
        MarkPatchContents(container_index);

        return kind;
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
