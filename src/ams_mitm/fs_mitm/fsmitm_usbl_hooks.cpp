/*
 * Copyright (c) Atmosphère-NX
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
#include <stratosphere/fssrv/fssrv_interface_adapters.hpp>
#include "fs_shim.h"
#include "fsmitm_usbl_hooks.hpp"
#include "fsmitm_usbl_content.hpp"
#include "fsmitm_usbl_content_storage_fs.hpp"
#include "../usbl/nca/usbl_external_keys.hpp"
#include "../usbl/usbl_log.hpp"

namespace ams::mitm::fs::usbl_hooks {

    using namespace ams::fs;

    namespace {

        template<typename... Arguments>
        constexpr ALWAYS_INLINE auto MakeSharedFileSystem(Arguments &&... args) {
            return sf::CreateSharedObjectEmplaced<ams::fssrv::sf::IFileSystem, ams::fssrv::impl::FileSystemInterfaceAdapter>(std::forward<Arguments>(args)...);
        }

        template<typename... Arguments>
        constexpr ALWAYS_INLINE auto MakeSharedStorage(Arguments &&... args) {
            return sf::CreateSharedObjectEmplaced<ams::fssrv::sf::IStorage, ams::fssrv::impl::StorageInterfaceAdapter>(std::forward<Arguments>(args)...);
        }

        /* FS never sees these requests, so borrow a real object (and its id) for domain bookkeeping and keep it alive alongside ours. */
        Result BorrowObjectId(FsFileSystem *out, Service *fwd) {
            R_SUCCEED_IF(R_SUCCEEDED(fsOpenSdCardFileSystemFwd(fwd, out)));
            R_RETURN(fsOpenSdCardFileSystem(out));
        }

        template<typename T>
        std::shared_ptr<T> HoldWithObjectIdOwner(std::shared_ptr<T> &&target, FsFileSystem &owner) {
            struct Holder {
                std::shared_ptr<T> target;
                RemoteFileSystem object_id_owner;

                Holder(std::shared_ptr<T> &&t, FsFileSystem &o) : target(std::move(t)), object_id_owner(o) { /* ... */ }
            };
            auto holder = std::make_shared<Holder>(std::move(target), owner);
            return std::shared_ptr<T>(holder, holder->target.get());
        }

        Result ReturnFileSystem(FileSystemOut &out, Service *fwd, std::shared_ptr<fsa::IFileSystem> &&file_system) {
            FsFileSystem owner;
            R_TRY(BorrowObjectId(std::addressof(owner), fwd));
            const sf::cmif::DomainObjectId target_object_id{serviceGetObjectId(std::addressof(owner.s))};

            out.SetValue(MakeSharedFileSystem(HoldWithObjectIdOwner(std::move(file_system), owner), false), target_object_id);
            R_SUCCEED();
        }

        Result ReturnStorage(StorageOut &out, Service *fwd, std::shared_ptr<IStorage> &&storage) {
            FsFileSystem owner;
            R_TRY(BorrowObjectId(std::addressof(owner), fwd));
            const sf::cmif::DomainObjectId target_object_id{serviceGetObjectId(std::addressof(owner.s))};

            out.SetValue(MakeSharedStorage(HoldWithObjectIdOwner(std::move(storage), owner)), target_object_id);
            R_SUCCEED();
        }

    }

    Result OpenFileSystemByPath(FileSystemOut &out, Service *fwd, const fssrv::sf::Path &path, u32 file_system_proxy_type) {
        ncm::ContentId content_id;
        R_UNLESS(IsUsblContentPath(std::addressof(content_id), path.str), sm::mitm::ResultShouldForwardToSession());

        std::shared_ptr<fsa::IFileSystem> content_fs;
        R_TRY(OpenUsblContentFileSystem(std::addressof(content_fs), content_id, static_cast<FsFileSystemType>(file_system_proxy_type)));
        R_RETURN(ReturnFileSystem(out, fwd, std::move(content_fs)));
    }

    Result OpenFileSystemWithPatch(FileSystemOut &out, Service *fwd, ncm::ProgramId program_id, u32 file_system_proxy_type) {
        R_UNLESS(IsUsblProgram(program_id), sm::mitm::ResultShouldForwardToSession());

        std::shared_ptr<fsa::IFileSystem> content_fs;
        R_TRY(OpenUsblProgramFileSystem(std::addressof(content_fs), program_id, static_cast<FsFileSystemType>(file_system_proxy_type)));
        R_RETURN(ReturnFileSystem(out, fwd, std::move(content_fs)));
    }

    Result OpenContentStorageFileSystem(FileSystemOut &out, Service *fwd, u32 content_storage_id) {
        /* NCAs served from USB must also exist as files in the SD card content storage. */
        R_UNLESS(content_storage_id == FsContentStorageId_SdCard, sm::mitm::ResultShouldForwardToSession());

        FsFileSystem content_fs;
        R_TRY(fsOpenContentStorageFileSystemFwd(fwd, std::addressof(content_fs), FsContentStorageId_SdCard));
        const sf::cmif::DomainObjectId target_object_id{serviceGetObjectId(std::addressof(content_fs.s))};

        auto overlay = std::make_shared<UsblContentStorageFileSystem>(std::make_unique<RemoteFileSystem>(content_fs));
        R_UNLESS(overlay != nullptr, ResultAllocationMemoryFailedNew());

        out.SetValue(MakeSharedFileSystem(std::move(overlay), false), target_object_id);
        R_SUCCEED();
    }

    Result OpenProgramDataStorage(StorageOut &out, Service *fwd, ncm::ProgramId program_id) {
        constexpr auto FileSystemTypeRom = static_cast<FsFileSystemType>(1);

        R_UNLESS(IsUsblProgram(program_id), sm::mitm::ResultShouldForwardToSession());

        std::shared_ptr<IStorage> romfs_storage;
        if (const Result result = OpenUsblProgramContentStorage(std::addressof(romfs_storage), program_id, FileSystemTypeRom); R_FAILED(result)) {
            usbl::Log("fs.mitm: cannot open RomFS of %016" PRIx64 ": 2%03d-%04d", program_id.value, result.GetModule(), result.GetDescription());
            R_THROW(result);
        }
        R_RETURN(ReturnStorage(out, fwd, std::move(romfs_storage)));
    }

    Result OpenAddOnContentDataStorage(StorageOut &out, Service *fwd, ncm::DataId data_id) {
        R_UNLESS(IsUsblAddOnContent(data_id), sm::mitm::ResultShouldForwardToSession());

        std::shared_ptr<IStorage> romfs_storage;
        if (const Result result = OpenUsblAddOnContentStorage(std::addressof(romfs_storage), data_id); R_FAILED(result)) {
            usbl::Log("fs.mitm: cannot open add-on content %016" PRIx64 ": 2%03d-%04d", data_id.value, result.GetModule(), result.GetDescription());
            R_THROW(result);
        }
        R_RETURN(ReturnStorage(out, fwd, std::move(romfs_storage)));
    }

    Result OpenDataStorageByPath(StorageOut &out, Service *fwd, const fssrv::sf::Path &path, u32 file_system_proxy_type) {
        ncm::ContentId content_id;
        R_UNLESS(IsUsblContentPath(std::addressof(content_id), path.str), sm::mitm::ResultShouldForwardToSession());

        std::shared_ptr<IStorage> content_storage;
        R_TRY(OpenUsblContentStorage(std::addressof(content_storage), content_id, static_cast<FsFileSystemType>(file_system_proxy_type)));
        R_RETURN(ReturnStorage(out, fwd, std::move(content_storage)));
    }

    Result GetRightsIdAndKeyGenerationByPath(sf::Out<ams::fs::RightsId> &out, sf::Out<u8> &out_key_generation, const fssrv::sf::FspPath &path) {
        /* FS cannot read USB-served NCAs; gamecard content has no rights id, eShop content carries its own. */
        UsblContentInfo info;
        R_UNLESS(FindUsblContentPath(std::addressof(info), path.str), sm::mitm::ResultShouldForwardToSession());

        out.SetValue(info.rights_id);
        out_key_generation.SetValue(info.key_generation);
        R_SUCCEED();
    }

    Result GetProgramId(sf::Out<ncm::ProgramId> &out, const fssrv::sf::FspPath &path) {
        /* FS would open the NCA header at this path, which does not exist on the SD card. */
        UsblContentInfo info;
        R_UNLESS(FindUsblContentPath(std::addressof(info), path.str), sm::mitm::ResultShouldForwardToSession());

        out.SetValue(info.program_id);
        R_SUCCEED();
    }

    void RegisterExternalKey(const u8 *rights_id_data, const spl::AccessKey &access_key, ncm::ProgramId client_program_id) {
        ams::fs::RightsId rights_id;
        std::memcpy(rights_id.data, rights_id_data, sizeof(rights_id.data));

        usbl::nca::RegisterExternalKey(rights_id, access_key);
        usbl::Log("fs.mitm: titlekey %016" PRIx64 "%016" PRIx64 " registered by %016" PRIx64, util::ConvertToBigEndian(rights_id.data64[0]), util::ConvertToBigEndian(rights_id.data64[1]), client_program_id.value);
    }

    void UnregisterAllExternalKeys(ncm::ProgramId client_program_id) {
        usbl::nca::UnregisterAllExternalKeys();
        usbl::Log("fs.mitm: all titlekeys unregistered by %016" PRIx64, client_program_id.value);
    }

}
