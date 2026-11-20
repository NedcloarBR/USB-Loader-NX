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

/* USB Loader NX entry points called from fs_mitm_service.cpp. Each returns sm::mitm::ResultShouldForwardToSession
 * when the request is not about USB-served content, so the caller falls through to Atmosphère's own handling. */
namespace ams::mitm::fs::usbl_hooks {

    using FileSystemOut = sf::Out<sf::SharedPointer<ams::fssrv::sf::IFileSystem>>;
    using StorageOut    = sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorage>>;

    inline bool IsHandled(Result result) {
        return !sm::mitm::ResultShouldForwardToSession::Includes(result);
    }

    /* OpenFileSystemWithId(Obsolete): a USB-served NCA by its SD card content path. */
    Result OpenFileSystemByPath(FileSystemOut &out, Service *fwd, const fssrv::sf::Path &path, u32 file_system_proxy_type);
    /* OpenFileSystemWithPatch: a USB-served program by id. */
    Result OpenFileSystemWithPatch(FileSystemOut &out, Service *fwd, ncm::ProgramId program_id, u32 file_system_proxy_type);
    /* OpenContentStorageFileSystem: the SD card content folder, overlaid with the USB-served NCAs. */
    Result OpenContentStorageFileSystem(FileSystemOut &out, Service *fwd, u32 content_storage_id);

    /* OpenDataStorageByCurrentProcess/ByProgramId/WithProgramIndex: the program's RomFS (with its update laid over).
     * With `allow_mods` (the client's override status, as Atmosphère decides it for installed games), romfs mods in
     * sd:/atmosphere/contents/<id>/ are laid over it with Atmosphère's LayeredFS. */
    Result OpenProgramDataStorage(StorageOut &out, Service *fwd, ncm::ProgramId program_id, bool allow_mods);
    /* OpenDataStorageByDataId: add-on content, with its romfs mods likewise. */
    Result OpenAddOnContentDataStorage(StorageOut &out, Service *fwd, ncm::DataId data_id, bool allow_mods);
    /* OpenDataStorageByPath. */
    Result OpenDataStorageByPath(StorageOut &out, Service *fwd, const fssrv::sf::Path &path, u32 file_system_proxy_type);

    Result GetRightsIdAndKeyGenerationByPath(sf::Out<ams::fs::RightsId> &out, sf::Out<u8> &out_key_generation, const fssrv::sf::FspPath &path);
    Result GetProgramId(sf::Out<ncm::ProgramId> &out, const fssrv::sf::FspPath &path);

    /* Titlekeys es hands to FS (607/608): copied for USB content; FS still receives them. */
    void RegisterExternalKey(const u8 *rights_id, const spl::AccessKey &access_key, ncm::ProgramId client_program_id);
    void UnregisterAllExternalKeys(ncm::ProgramId client_program_id);

}
