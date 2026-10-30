/*
 * Copyright (c) Atmosphère-NX
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
#include <stratosphere/fssrv/fssrv_interface_adapters.hpp>

namespace ams::mitm::fs {

    /* On the wire the rights id comes first; declared as RightsId (8-aligned) + AccessKey, the alignment-sorted layout reads them swapped. */
    struct ExternalKeyRegistration {
        u8 rights_id[sizeof(ams::fs::RightsId)];
        spl::AccessKey access_key;
    };
    static_assert(sizeof(ExternalKeyRegistration) == 0x20 && alignof(ExternalKeyRegistration) == alignof(u8));

}

#define AMS_FS_MITM_INTERFACE_INFO(C, H)                                                                                                                                                                                                                                                                                                                   \
    AMS_SF_METHOD_INFO(C, H,   7, Result, OpenFileSystemWithPatch,                     (sf::Out<sf::SharedPointer<ams::fssrv::sf::IFileSystem>> out, ncm::ProgramId program_id, u32 file_system_proxy_type),                                                     (out, program_id, file_system_proxy_type),                           hos::Version_2_0_0)  \
    AMS_SF_METHOD_INFO(C, H,   8, Result, OpenFileSystemWithIdObsolete,                (sf::Out<sf::SharedPointer<ams::fssrv::sf::IFileSystem>> out, const fssrv::sf::Path &path, ncm::ProgramId program_id, u32 file_system_proxy_type),                        (out, path, program_id, file_system_proxy_type),                     hos::Version_2_0_0)  \
    AMS_SF_METHOD_INFO(C, H,  10, Result, OpenFileSystemWithId,                        (sf::Out<sf::SharedPointer<ams::fssrv::sf::IFileSystem>> out, const fssrv::sf::Path &path, u8 content_attributes, ncm::ProgramId program_id, u32 file_system_proxy_type), (out, path, content_attributes, program_id, file_system_proxy_type), hos::Version_16_0_0) \
    AMS_SF_METHOD_INFO(C, H,  18, Result, OpenSdCardFileSystem,                        (sf::Out<sf::SharedPointer<ams::fssrv::sf::IFileSystem>> out),                                                                                                            (out))                                                                                    \
    AMS_SF_METHOD_INFO(C, H,  51, Result, OpenSaveDataFileSystem,                      (sf::Out<sf::SharedPointer<ams::fssrv::sf::IFileSystem>> out, u8 space_id, const ams::fs::SaveDataAttribute &attribute),                                                  (out, space_id, attribute))                                                               \
    AMS_SF_METHOD_INFO(C, H,  12, Result, OpenBisStorage,                              (sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorage>> out, u32 bis_partition_id),                                                                                         (out, bis_partition_id))                                                                  \
    AMS_SF_METHOD_INFO(C, H, 110, Result, OpenContentStorageFileSystem,                (sf::Out<sf::SharedPointer<ams::fssrv::sf::IFileSystem>> out, u32 content_storage_id), (out, content_storage_id)) \
    AMS_SF_METHOD_INFO(C, H, 200, Result, OpenDataStorageByCurrentProcess,             (sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorage>> out),                                                                                                               (out))                                                                                    \
    AMS_SF_METHOD_INFO(C, H, 201, Result, OpenDataStorageByProgramId,                  (sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorage>> out, ncm::ProgramId program_id),                                                                                    (out, program_id),                                                   hos::Version_3_0_0)  \
    AMS_SF_METHOD_INFO(C, H, 202, Result, OpenDataStorageByDataId,                     (sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorage>> out, ncm::DataId data_id, u8 storage_id),                                                                           (out, data_id, storage_id))                                                               \
    AMS_SF_METHOD_INFO(C, H, 205, Result, OpenDataStorageWithProgramIndex,             (sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorage>> out, u8 program_index),                                                                                             (out, program_index),                                                hos::Version_7_0_0)  \
    AMS_SF_METHOD_INFO(C, H, 206, Result, OpenDataStorageByPath,                       (sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorage>> out, const fssrv::sf::Path &path, u8 content_attributes, u32 file_system_proxy_type),                               (out, path, content_attributes, file_system_proxy_type),             hos::Version_13_0_0) \
    AMS_SF_METHOD_INFO(C, H, 210, Result, OpenDataStorageByCurrentProcessForBatchRead, (sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorageForBatchRead>> out),                                                                                                   (out),                                                               hos::Version_23_0_0) \
    AMS_SF_METHOD_INFO(C, H, 211, Result, OpenDataStorageByProgramIdForBatchRead,      (sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorageForBatchRead>> out, ncm::ProgramId program_id),                                                                        (out, program_id),                                                   hos::Version_23_0_0) \
    AMS_SF_METHOD_INFO(C, H, 212, Result, OpenDataStorageWithProgramIndexForBatchRead, (sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorageForBatchRead>> out, u8 program_index),                                                                                 (out, program_index),                                                hos::Version_23_0_0) \
    AMS_SF_METHOD_INFO(C, H, 213, Result, OpenDataStorageByPathForBatchRead,           (sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorageForBatchRead>> out, const fssrv::sf::Path &path, u8 content_attributes, u32 file_system_proxy_type),                   (out, path, content_attributes, file_system_proxy_type),             hos::Version_23_0_0) \
    AMS_SF_METHOD_INFO(C, H, 610, Result, GetRightsIdAndKeyGenerationByPath,           (sf::Out<ams::fs::RightsId> out, sf::Out<u8> out_key_generation, const fssrv::sf::FspPath &path, ams::fs::ContentAttributes attr), (out, out_key_generation, path, attr), hos::Version_16_0_0) \
    AMS_SF_METHOD_INFO(C, H, 607, Result, RegisterExternalKey,                         (const ams::mitm::fs::ExternalKeyRegistration &registration),                                    (registration),          hos::Version_2_0_0) \
    AMS_SF_METHOD_INFO(C, H, 608, Result, UnregisterAllExternalKey,                    (),                                                                                              (),                      hos::Version_2_0_0) \
    AMS_SF_METHOD_INFO(C, H, 618, Result, GetProgramId,                                (sf::Out<ncm::ProgramId> out, const fssrv::sf::FspPath &path, ams::fs::ContentAttributes attr), (out, path, attr), hos::Version_17_0_0) \
    AMS_SF_METHOD_INFO(C, H, 810, Result, RegisterProgramIndexMapInfo,                 (const sf::InBuffer &info_buffer, s32 info_count),                                                                                                                        (info_buffer, info_count),                                           hos::Version_7_0_0)


AMS_SF_DEFINE_MITM_INTERFACE(ams::mitm::fs, IFsMitmInterface, AMS_FS_MITM_INTERFACE_INFO, 0x7DF34ED2)

namespace ams::mitm::fs {

    class FsMitmService  : public sf::MitmServiceImplBase {
        public:
            using MitmServiceImplBase::MitmServiceImplBase;
        public:
            static constexpr ALWAYS_INLINE bool ShouldMitmProgramId(const ncm::ProgramId program_id) {
                /* We want to mitm everything that isn't a system-module. */
                if (!ncm::IsSystemProgramId(program_id)) {
                    return true;
                }

                /* We want to mitm ns, to intercept SD card requests and program index map info registration. */
                if (program_id == ncm::SystemProgramId::Ns) {
                    return true;
                }

                /* USB Loader NX: es hands titlekeys to FS (RegisterExternalKey) over a session opened at boot. */
                if (program_id == ncm::SystemProgramId::Es) {
                    return true;
                }

                /* We want to mitm settings, to intercept CAL0. */
                if (program_id == ncm::SystemProgramId::Settings) {
                    return true;
                }

                /* We want to mitm sdb, to support sd-romfs redirection of common system archives (like system font, etc). */
                /* NOTE: In 16.0.0+, this was moved to glue. */
                if (program_id == ncm::SystemProgramId::Sdb || program_id == ncm::SystemProgramId::Glue) {
                    return true;
                }

                return false;
            }

            static bool ShouldMitm(const sm::MitmProcessInfo &client_info) {
                static std::atomic_bool has_launched_qlaunch = false;

                /* TODO: intercepting everything seems to cause issues with sleep mode, for some reason. */
                /* Figure out why, and address it. */
                /* TODO: This may be because pre-rewrite code really mismanaged domain objects in a way that would cause bad things. */
                /* Need to verify if this is fixed now. */
                if (client_info.program_id == ncm::SystemAppletId::Qlaunch || client_info.program_id == ncm::SystemAppletId::MaintenanceMenu) {
                    has_launched_qlaunch = true;
                }

                return has_launched_qlaunch || ShouldMitmProgramId(client_info.program_id);
            }
        public:
            /* Overridden commands. */
            Result OpenFileSystemWithPatch(sf::Out<sf::SharedPointer<ams::fssrv::sf::IFileSystem>> out, ncm::ProgramId program_id, u32 file_system_proxy_type);
            Result OpenFileSystemWithIdObsolete(sf::Out<sf::SharedPointer<ams::fssrv::sf::IFileSystem>> out, const fssrv::sf::Path &path, ncm::ProgramId program_id, u32 file_system_proxy_type);
            Result OpenFileSystemWithId(sf::Out<sf::SharedPointer<ams::fssrv::sf::IFileSystem>> out, const fssrv::sf::Path &path, u8 content_attributes, ncm::ProgramId program_id, u32 file_system_proxy_type);
            Result OpenSdCardFileSystem(sf::Out<sf::SharedPointer<ams::fssrv::sf::IFileSystem>> out);
            Result OpenSaveDataFileSystem(sf::Out<sf::SharedPointer<ams::fssrv::sf::IFileSystem>> out, u8 space_id, const ams::fs::SaveDataAttribute &attribute);
            Result OpenContentStorageFileSystem(sf::Out<sf::SharedPointer<ams::fssrv::sf::IFileSystem>> out, u32 content_storage_id);
            Result OpenBisStorage(sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorage>> out, u32 bis_partition_id);
            Result OpenDataStorageByCurrentProcess(sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorage>> out);
            Result OpenDataStorageByProgramId(sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorage>> out, ncm::ProgramId program_id);
            Result OpenDataStorageByDataId(sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorage>> out, ncm::DataId data_id, u8 storage_id);
            Result OpenDataStorageWithProgramIndex(sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorage>> out, u8 program_index);
            Result OpenDataStorageByPath(sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorage>> out, const fssrv::sf::Path &path, u8 content_attributes, u32 file_system_proxy_type);
            Result RegisterExternalKey(const ExternalKeyRegistration &registration);
            Result UnregisterAllExternalKey();
            Result GetProgramId(sf::Out<ncm::ProgramId> out, const fssrv::sf::FspPath &path, ams::fs::ContentAttributes attr);
            Result GetRightsIdAndKeyGenerationByPath(sf::Out<ams::fs::RightsId> out, sf::Out<u8> out_key_generation, const fssrv::sf::FspPath &path, ams::fs::ContentAttributes attr);
            Result OpenDataStorageByCurrentProcessForBatchRead(sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorageForBatchRead>> out);
            Result OpenDataStorageByProgramIdForBatchRead(sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorageForBatchRead>> out, ncm::ProgramId program_id);
            Result OpenDataStorageWithProgramIndexForBatchRead(sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorageForBatchRead>> out, u8 program_index);
            Result OpenDataStorageByPathForBatchRead(sf::Out<sf::SharedPointer<ams::fssrv::sf::IStorageForBatchRead>> out, const fssrv::sf::Path &path, u8 content_attributes, u32 file_system_proxy_type);
            Result RegisterProgramIndexMapInfo(const sf::InBuffer &info_buffer, s32 info_count);
    };
    static_assert(IsIFsMitmInterface<FsMitmService>);

}
