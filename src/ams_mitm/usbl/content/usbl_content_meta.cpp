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
#include "usbl_content_meta.hpp"
#include "usbl_content_index.hpp"
#include "../nca/usbl_nca_filesystem.hpp"
#include "../storage/usbl_volume.hpp"
#include "../usbl_log.hpp"
#include "../usbl_results.hpp"

namespace ams::mitm::usbl::content {

    namespace {

        /* ncm passes an opaque path to the mount function; ours is the program id in hex. */
        constexpr size_t ProgramIdPathLength = 2 * sizeof(u64);
        constexpr s32 MetaSectionIndex = 0;

        Result MountIndexedContentMeta(const char *mount_name, const char *path, fs::ContentAttributes attr) {
            AMS_UNUSED(attr);
            R_UNLESS(path != nullptr && std::strlen(path) == ProgramIdPathLength, fs::ResultInvalidPath());

            char *end = nullptr;
            const ncm::ProgramId program_id = { std::strtoull(path, std::addressof(end), 16) };
            R_UNLESS(end == path + ProgramIdPathLength, fs::ResultInvalidPath());

            std::shared_ptr<fs::IStorage> raw_nca;
            R_TRY(OpenIndexedContentStorage(std::addressof(raw_nca), program_id, NcaContentType::Meta, ContentLayer::Current));

            std::shared_ptr<fssystem::NcaReader> reader;
            R_TRY(nca::OpenReader(std::addressof(reader), std::move(raw_nca)));

            std::shared_ptr<fs::fsa::IFileSystem> meta_fs;
            R_TRY(nca::OpenSectionFileSystem(std::addressof(meta_fs), std::move(reader), MetaSectionIndex));

            auto holder = std::make_unique<fssystem::ForwardingFileSystem>(std::move(meta_fs));
            R_UNLESS(holder != nullptr, fs::ResultAllocationMemoryFailedNew());

            R_RETURN(fs::fsa::Register(mount_name, std::move(holder)));
        }

        const char *GetContentTypeName(ncm::ContentType type) {
            switch (type) {
                case ncm::ContentType::Meta:             return "Meta";
                case ncm::ContentType::Program:          return "Program";
                case ncm::ContentType::Data:             return "Data";
                case ncm::ContentType::Control:          return "Control";
                case ncm::ContentType::HtmlDocument:     return "HtmlDocument";
                case ncm::ContentType::LegalInformation: return "LegalInformation";
                case ncm::ContentType::DeltaFragment:    return "DeltaFragment";
                default:                                 return "Unknown";
            }
        }

        void LogContentMeta(ncm::ProgramId program_id) {
            ncm::AutoBuffer meta;
            if (const Result result = ReadPackagedContentMeta(std::addressof(meta), program_id); R_FAILED(result)) {
                Log("cnmt %016" PRIx64 ": read failed 2%03d-%04d", program_id.value, result.GetModule(), result.GetDescription());
                return;
            }

            const ncm::PackagedContentMetaReader reader(meta.Get(), meta.GetSize());
            const auto key = reader.GetKey();
            Log("cnmt %016" PRIx64 " v%u type=0x%02x: %zu contents", key.id, key.version, static_cast<u8>(key.type), reader.GetContentCount());

            /* What ns checks before launching: the firmware the game needs, and for applications the minimum version of themselves. */
            if (key.type == ncm::ContentMetaType::Application) {
                const auto *header = reader.GetExtendedHeader<ncm::ApplicationMetaExtendedHeader>();
                Log("  requires system v%u (%u.%u.%u), application v%u", header->required_system_version,
                    header->required_system_version >> 26, (header->required_system_version >> 20) & 0x3F, (header->required_system_version >> 16) & 0xF,
                    header->required_application_version);
            } else if (key.type == ncm::ContentMetaType::Patch) {
                const u32 required_system_version = reader.GetExtendedHeader<ncm::PatchMetaExtendedHeader>()->required_system_version;
                Log("  requires system v%u (%u.%u.%u)", required_system_version, required_system_version >> 26, (required_system_version >> 20) & 0x3F, (required_system_version >> 16) & 0xF);
            }

            for (size_t i = 0; i < reader.GetContentCount(); ++i) {
                const auto *info = reader.GetContentInfo(i);
                const auto content_id = ncm::GetContentIdString(info->GetId());
                Log("  %-16s %s size=%" PRIu64, GetContentTypeName(info->GetType()), content_id.data, info->info.GetSize());
            }
        }

    }

    void InitializeContentMetaReader() {
        ncm::SetMountContentMetaFunction(MountIndexedContentMeta);
    }

    Result ReadPackagedContentMeta(ncm::AutoBuffer *out, ncm::ProgramId program_id) {
        char path[ProgramIdPathLength + 1];
        util::SNPrintf(path, sizeof(path), "%016" PRIx64, program_id.value);

        R_RETURN(ncm::ReadContentMetaPathAlongWithExtendedDataAndDigest(out, path, fs::ContentAttributes_None));
    }

    void LogIndexedContentMetas() {
        std::scoped_lock lk(GetVolumeLock());

        for (s32 i = 0; i < GetIndexedContentCount(); ++i) {
            IndexedContent content;
            if (R_SUCCEEDED(GetIndexedContent(std::addressof(content), i)) && content.content_type == NcaContentType::Meta) {
                LogContentMeta(content.program_id);
            }
        }
    }

}
