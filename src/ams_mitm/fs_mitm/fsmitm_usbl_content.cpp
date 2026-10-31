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
#include "fsmitm_usbl_content.hpp"
#include "fsmitm_usbl_badge_fs.hpp"
#include "../usbl/nca/usbl_external_keys.hpp"
#include "../usbl/nca/usbl_nca_filesystem.hpp"
#include "../usbl/usbl_content_api.hpp"

namespace ams::mitm::fs {

    namespace {

        constexpr const char NcaExtension[] = ".nca";

        using NcaContentType = fssystem::NcaHeader::ContentType;

        NcaContentType GetNcaContentType(FsFileSystemType filesystem_type) {
            switch (filesystem_type) {
                case FsFileSystemType_ContentControl: return NcaContentType::Control;
                case FsFileSystemType_ContentManual:  return NcaContentType::Manual;
                case FsFileSystemType_ContentMeta:    return NcaContentType::Meta;
                case FsFileSystemType_ContentData:    return NcaContentType::Data;
                default:                              return NcaContentType::Program;
            }
        }

        constexpr s32 RomSectionIndex = 1;

        /* Same partition selection FS uses for each filesystem type (libnx has no names for Code = 0 and Rom = 1). */
        s32 GetSectionIndex(FsFileSystemType filesystem_type) {
            constexpr u32 FileSystemTypeRom = 1;

            switch (static_cast<u32>(filesystem_type)) {
                case FileSystemTypeRom:     return RomSectionIndex;
                case FsFileSystemType_Logo: return 2;
                default:                    return 0;
            }
        }

    }

    bool FindUsblContentPath(UsblContentInfo *out, const char *path) {
        const char *extension = std::strstr(path, NcaExtension);
        if (extension == nullptr || extension - path < static_cast<ptrdiff_t>(ncm::ContentIdStringLength)) {
            return false;
        }

        const auto content_id = ncm::GetContentIdFromString(extension - ncm::ContentIdStringLength, ncm::ContentIdStringLength);
        usbl::ServedContent content;
        if (!content_id.has_value() || !usbl::FindServedContent(std::addressof(content), *content_id)) {
            return false;
        }

        *out = { .content_id = content.content_id, .program_id = content.program_id, .rights_id = content.rights_id, .key_generation = content.key_generation };
        return true;
    }

    bool IsUsblContentPath(ncm::ContentId *out, const char *path) {
        UsblContentInfo info;
        if (!FindUsblContentPath(std::addressof(info), path)) {
            return false;
        }

        *out = info.content_id;
        return true;
    }

    namespace {

        Result CreateReader(std::shared_ptr<fssystem::NcaReader> *out, std::shared_ptr<ams::fs::IStorage> raw_nca) {
            /* A re-signed header (repacked dump) only passes USB Loader NX's own check. */
            R_TRY_CATCH(fssystem::GetFileSystemCreatorInterfaces()->storage_on_nca_creator->CreateNcaReader(out, raw_nca)) {
                R_CATCH(ams::fs::ResultNcaHeaderSignature1VerificationFailed) {
                    R_TRY(usbl::nca::OpenReader(out, std::move(raw_nca)));
                }
            } R_END_TRY_CATCH;

            R_RETURN(usbl::nca::ApplyExternalKey(**out));
        }

        Result OpenSection(std::shared_ptr<ams::fs::IStorage> *out, fssystem::NcaFsHeaderReader *out_header_reader, std::shared_ptr<ams::fs::IStorage> raw_nca, FsFileSystemType filesystem_type) {
            std::shared_ptr<fssystem::NcaReader> reader;
            R_TRY(CreateReader(std::addressof(reader), std::move(raw_nca)));

            std::shared_ptr<fssystem::IAsynchronousAccessSplitter> splitter;
            R_RETURN(fssystem::GetFileSystemCreatorInterfaces()->storage_on_nca_creator->Create(out, std::addressof(splitter), out_header_reader, std::move(reader), GetSectionIndex(filesystem_type)));
        }

        /* The program's own section, except the RomFS of an update: only changes (BKTR) laid over the base game's RomFS. */
        Result OpenProgramSection(std::shared_ptr<ams::fs::IStorage> *out, fssystem::NcaFsHeaderReader *out_header_reader, ncm::ProgramId program_id, FsFileSystemType filesystem_type) {
            const auto content_type = GetNcaContentType(filesystem_type);

            std::shared_ptr<ams::fs::IStorage> raw_nca;
            R_TRY(usbl::OpenServedProgramContentStorage(std::addressof(raw_nca), program_id, content_type));

            if (content_type != NcaContentType::Program || GetSectionIndex(filesystem_type) != RomSectionIndex || !usbl::IsServedPatchedProgram(program_id)) {
                R_RETURN(OpenSection(out, out_header_reader, std::move(raw_nca), filesystem_type));
            }

            std::shared_ptr<ams::fs::IStorage> raw_base_nca;
            R_TRY(usbl::OpenServedBaseProgramContentStorage(std::addressof(raw_base_nca), program_id, content_type));

            std::shared_ptr<fssystem::NcaReader> base_reader;
            std::shared_ptr<fssystem::NcaReader> patch_reader;
            R_TRY(CreateReader(std::addressof(base_reader), std::move(raw_base_nca)));
            R_TRY(CreateReader(std::addressof(patch_reader), std::move(raw_nca)));

            std::shared_ptr<fssystem::IAsynchronousAccessSplitter> splitter;
            R_RETURN(fssystem::GetFileSystemCreatorInterfaces()->storage_on_nca_creator->CreateWithPatch(out, std::addressof(splitter), out_header_reader, std::move(base_reader), std::move(patch_reader), RomSectionIndex));
        }

        Result MountStorage(std::shared_ptr<ams::fs::fsa::IFileSystem> *out, std::shared_ptr<ams::fs::IStorage> storage, const fssystem::NcaFsHeaderReader &header_reader) {
            const auto * const creators = fssystem::GetFileSystemCreatorInterfaces();
            switch (header_reader.GetFsType()) {
                case fssystem::NcaFsHeader::FsType::PartitionFs: R_RETURN(creators->partition_fs_creator->Create(out, std::move(storage)));
                case fssystem::NcaFsHeader::FsType::RomFs:       R_RETURN(creators->rom_fs_creator->Create(out, std::move(storage)));
                default:                                         R_THROW(ams::fs::ResultInvalidNcaFileSystemType());
            }
        }

        Result MountSection(std::shared_ptr<ams::fs::fsa::IFileSystem> *out, std::shared_ptr<ams::fs::IStorage> raw_nca, FsFileSystemType filesystem_type) {
            std::shared_ptr<ams::fs::IStorage> storage;
            fssystem::NcaFsHeaderReader header_reader;
            R_TRY(OpenSection(std::addressof(storage), std::addressof(header_reader), std::move(raw_nca), filesystem_type));
            R_RETURN(MountStorage(out, std::move(storage), header_reader));
        }

    }

    bool IsUsblProgram(ncm::ProgramId program_id) {
        return usbl::IsServedProgram(program_id);
    }

    Result OpenUsblProgramFileSystem(std::shared_ptr<ams::fs::fsa::IFileSystem> *out, ncm::ProgramId program_id, FsFileSystemType filesystem_type) {
        std::shared_ptr<ams::fs::IStorage> storage;
        fssystem::NcaFsHeaderReader header_reader;
        R_TRY(OpenProgramSection(std::addressof(storage), std::addressof(header_reader), program_id, filesystem_type));

        std::shared_ptr<ams::fs::fsa::IFileSystem> section_fs;
        R_TRY(MountStorage(std::addressof(section_fs), std::move(storage), header_reader));

        usbl::ServedContent content;
        if (filesystem_type == FsFileSystemType_ContentControl && usbl::FindServedProgramContent(std::addressof(content), program_id, GetNcaContentType(filesystem_type))) {
            section_fs = MakeUsblBadgedControlFileSystem(std::move(section_fs), content.source);
        }

        *out = std::move(section_fs);
        R_SUCCEED();
    }

    Result OpenUsblProgramContentStorage(std::shared_ptr<ams::fs::IStorage> *out, ncm::ProgramId program_id, FsFileSystemType filesystem_type) {
        fssystem::NcaFsHeaderReader header_reader;
        R_RETURN(OpenProgramSection(out, std::addressof(header_reader), program_id, filesystem_type));
    }

    Result OpenUsblContentFileSystem(std::shared_ptr<ams::fs::fsa::IFileSystem> *out, ncm::ContentId content_id, FsFileSystemType filesystem_type) {
        std::shared_ptr<ams::fs::IStorage> raw_nca;
        R_TRY(usbl::OpenServedContentStorage(std::addressof(raw_nca), content_id));

        std::shared_ptr<ams::fs::fsa::IFileSystem> section_fs;
        R_TRY(MountSection(std::addressof(section_fs), std::move(raw_nca), filesystem_type));

        usbl::ServedContent content;
        if (filesystem_type == FsFileSystemType_ContentControl && usbl::FindServedContent(std::addressof(content), content_id)) {
            section_fs = MakeUsblBadgedControlFileSystem(std::move(section_fs), content.source);
        }

        *out = std::move(section_fs);
        R_SUCCEED();
    }

    namespace {

        /* Add-on content NCAs are Data (older SDKs) or PublicData. */
        bool FindUsblAddOnContent(usbl::ServedContent *out, ncm::DataId data_id) {
            const ncm::ProgramId id = { data_id.value };
            return usbl::FindServedProgramContent(out, id, NcaContentType::PublicData) || usbl::FindServedProgramContent(out, id, NcaContentType::Data);
        }

    }

    bool IsUsblAddOnContent(ncm::DataId data_id) {
        usbl::ServedContent content;
        return FindUsblAddOnContent(std::addressof(content), data_id);
    }

    Result OpenUsblAddOnContentStorage(std::shared_ptr<ams::fs::IStorage> *out, ncm::DataId data_id) {
        usbl::ServedContent content;
        R_UNLESS(FindUsblAddOnContent(std::addressof(content), data_id), ams::fs::ResultTargetNotFound());
        R_RETURN(OpenUsblContentStorage(out, content.content_id, FsFileSystemType_ContentData));
    }

    Result OpenUsblRawContentStorage(std::shared_ptr<ams::fs::IStorage> *out, ncm::ContentId content_id) {
        R_RETURN(usbl::OpenServedContentStorage(out, content_id));
    }

    Result OpenUsblContentStorage(std::shared_ptr<ams::fs::IStorage> *out, ncm::ContentId content_id, FsFileSystemType filesystem_type) {
        std::shared_ptr<ams::fs::IStorage> raw_nca;
        R_TRY(usbl::OpenServedContentStorage(std::addressof(raw_nca), content_id));

        fssystem::NcaFsHeaderReader header_reader;
        R_RETURN(OpenSection(out, std::addressof(header_reader), std::move(raw_nca), filesystem_type));
    }

}
