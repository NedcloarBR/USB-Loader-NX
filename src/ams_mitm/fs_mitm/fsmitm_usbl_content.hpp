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

namespace ams::mitm::fs {

    /* USB Loader NX: content registered on the SD card whose NCA is served from a USB drive. */
    struct UsblContentInfo {
        ncm::ContentId content_id;
        ncm::ProgramId program_id;
        ams::fs::RightsId rights_id;
        u8 key_generation;
    };

    bool IsUsblContentPath(ncm::ContentId *out, const char *path);
    bool FindUsblContentPath(UsblContentInfo *out, const char *path);

    /* Mounts the section FS would use for filesystem_type, decrypted by fs.mitm's fssystem (signatures and hashes verified). */
    Result OpenUsblContentFileSystem(std::shared_ptr<ams::fs::fsa::IFileSystem> *out, ncm::ContentId content_id, FsFileSystemType filesystem_type);

    /* Path-less variant (OpenFileSystemWithPatch): FS would resolve the content through lr. */
    bool IsUsblProgram(ncm::ProgramId program_id);
    Result OpenUsblProgramFileSystem(std::shared_ptr<ams::fs::fsa::IFileSystem> *out, ncm::ProgramId program_id, FsFileSystemType filesystem_type);

    /* Section storage of the program's NCA (OpenDataStorageByCurrentProcess/ByProgramId/WithProgramIndex: the RomFS). */
    Result OpenUsblProgramContentStorage(std::shared_ptr<ams::fs::IStorage> *out, ncm::ProgramId program_id, FsFileSystemType filesystem_type);

    /* RomFS of an add-on content served from USB (OpenDataStorageByDataId); false when the data id is not served. */
    bool IsUsblAddOnContent(ncm::DataId data_id);
    Result OpenUsblAddOnContentStorage(std::shared_ptr<ams::fs::IStorage> *out, ncm::DataId data_id);

    /* Raw (still encrypted) NCA bytes, as stored inside the XCI. */
    Result OpenUsblRawContentStorage(std::shared_ptr<ams::fs::IStorage> *out, ncm::ContentId content_id);

    /* Same section, as a storage (OpenDataStorageByPath). */
    Result OpenUsblContentStorage(std::shared_ptr<ams::fs::IStorage> *out, ncm::ContentId content_id, FsFileSystemType filesystem_type);

}
