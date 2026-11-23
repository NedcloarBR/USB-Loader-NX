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
#include "fsmitm_usbl_sd_card_fs.hpp"
#include "../usbl/storage/usbl_usb_file_storage.hpp"
#include "../usbl/storage/usbl_volume.hpp"
#include "../usbl/usbl_disk_io.hpp"
#include "../usbl/usbl_log.hpp"
#include "../usbl/usbl_results.hpp"
#include "../usbl/usbl_usb_volume_manager.hpp"

namespace ams::mitm::fs {

    namespace {

        using ams::mitm::usbl::GetVolumeLock;

        /* "<drive>:/<path on the drive>" for FatFs. */
        constexpr size_t MaxFatFsPathLength = 0x310;

        enum class Target {
            SdCard,
            /* /usbloadernx/usb itself: one folder per mounted drive. */
            DriveList,
            Drive,
        };

        struct Route {
            Target target;
            s32 drive;
            /* The drive's usbloadernx/usb folder itself, which may not exist yet: then an empty folder. */
            bool is_shared_root;
            char fatfs_path[MaxFatFsPathLength];
        };

        /* Length of the /usbloadernx/usb prefix when `str` is that folder or inside it (case does not matter, as on the SD card), else 0. */
        size_t MatchUsbPrefix(const char *str) {
            const size_t prefix_length = std::strlen(::usbl::paths::HomebrewUsbDir);
            if (util::Strnicmp(str, ::usbl::paths::HomebrewUsbDir, static_cast<int>(prefix_length)) != 0) {
                return 0;
            }
            return str[prefix_length] == '\x00' || str[prefix_length] == '/' ? prefix_length : 0;
        }

        bool IsOnUsb(const ams::fs::Path &path) {
            return MatchUsbPrefix(path.GetString()) != 0;
        }

        /* Where a path of the SD card lands. A drive name that is not mounted is not found. */
        Result Resolve(Route *out, const ams::fs::Path &path) {
            out->target = Target::SdCard;
            out->drive = -1;
            out->is_shared_root = false;
            out->fatfs_path[0] = '\x00';

            const char *str = path.GetString();
            const size_t prefix_length = MatchUsbPrefix(str);
            R_SUCCEED_IF(prefix_length == 0);

            const char *rest = str + prefix_length;
            while (*rest == '/') {
                ++rest;
            }
            if (*rest == '\x00') {
                /* Only while a drive is plugged in: without one the folder does not exist (or is whatever the SD card has). */
                std::scoped_lock lk(GetVolumeLock());
                s32 drives[usbl::MaxDrives];
                if (usbl::ListMountedVolumes(drives, usbl::MaxDrives) > 0) {
                    out->target = Target::DriveList;
                }
                R_SUCCEED();
            }

            const char *slash = std::strchr(rest, '/');
            const size_t name_length = slash != nullptr ? static_cast<size_t>(slash - rest) : std::strlen(rest);
            const char *inside = slash != nullptr ? slash : "";
            out->is_shared_root = inside[0] == '\x00' || (inside[0] == '/' && inside[1] == '\x00');
            if (out->is_shared_root) {
                inside = "";
            }

            std::scoped_lock lk(GetVolumeLock());
            out->drive = usbl::FindMountedVolume(rest, name_length);
            R_UNLESS(out->drive >= 0, ams::fs::ResultPathNotFound());

            /* Only the drive's own usbloadernx/usb folder is shared, not the whole drive. */
            R_UNLESS(static_cast<size_t>(util::SNPrintf(out->fatfs_path, sizeof(out->fatfs_path), "%d:%s%s", out->drive, ::usbl::paths::HomebrewUsbDir, inside)) < sizeof(out->fatfs_path), ams::fs::ResultTooLongPath());
            out->target = Target::Drive;
            R_SUCCEED();
        }

        /* Missing names are what a homebrew checks for; anything else is a drive error. */
        Result ConvertFatFsLookupResult(FRESULT res) {
            switch (res) {
                case FR_OK:           R_SUCCEED();
                case FR_NO_FILE:
                case FR_NO_PATH:
                case FR_INVALID_NAME: R_THROW(ams::fs::ResultPathNotFound());
                default:              R_RETURN(usbl::ConvertFatFsResult(res));
            }
        }

        /* Requires the volume lock. The shared folder is a folder even before it exists on the drive. */
        Result StatOnDrive(FILINFO *out, const Route &route) {
            R_UNLESS(usbl::IsVolumeMounted(route.drive), usbl::ResultDriveDetached());
            const FRESULT res = f_stat(route.fatfs_path, out);
            if (route.is_shared_root && res != FR_OK) {
                std::memset(out, 0, sizeof(*out));
                out->fattrib = AM_DIR;
                R_SUCCEED();
            }
            R_RETURN(ConvertFatFsLookupResult(res));
        }

        /* FAT keeps local date and time in DOS form; days from 1970 by the civil calendar. */
        s64 ToPosixTime(WORD date, WORD time) {
            const s64 year = 1980 + (date >> 9), month = (date >> 5) & 0xF, day = date & 0x1F;
            const s64 y = year - (month <= 2 ? 1 : 0);
            const s64 era = y / 400;
            const s64 yoe = y - era * 400;
            const s64 doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
            const s64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            const s64 days = era * 146097 + doe - 719468;
            return days * 86400 + (time >> 11) * 3600 + ((time >> 5) & 0x3F) * 60 + (time & 0x1F) * 2;
        }

        class DriveFile final : public ams::fs::fsa::IFile, public ams::fs::impl::Newable {
            NON_COPYABLE(DriveFile);
            NON_MOVEABLE(DriveFile);
            private:
                std::unique_ptr<usbl::UsbFileStorage> m_storage;
                s64 m_size;
            public:
                DriveFile(std::unique_ptr<usbl::UsbFileStorage> &&storage, s64 size) : m_storage(std::move(storage)), m_size(size) { /* ... */ }

                virtual sf::cmif::DomainObjectId GetDomainObjectId() const override {
                    return sf::cmif::InvalidDomainObjectId;
                }
            private:
                virtual Result DoRead(size_t *out, s64 offset, void *buffer, size_t size, const ams::fs::ReadOption &option) override {
                    AMS_UNUSED(option);
                    R_UNLESS(offset >= 0, ams::fs::ResultOutOfRange());

                    const size_t read_size = offset < m_size ? std::min<size_t>(size, m_size - offset) : 0;
                    if (read_size > 0) {
                        R_TRY(m_storage->Read(offset, buffer, read_size));
                    }
                    *out = read_size;
                    R_SUCCEED();
                }

                virtual Result DoGetSize(s64 *out) override {
                    *out = m_size;
                    R_SUCCEED();
                }

                virtual Result DoFlush() override {
                    R_SUCCEED();
                }

                virtual Result DoWrite(s64 offset, const void *buffer, size_t size, const ams::fs::WriteOption &option) override {
                    AMS_UNUSED(offset, buffer, size, option);
                    R_THROW(ams::fs::ResultUnsupportedWriteForReadOnlyFileSystem());
                }

                virtual Result DoSetSize(s64 size) override {
                    AMS_UNUSED(size);
                    R_THROW(ams::fs::ResultUnsupportedWriteForReadOnlyFileSystem());
                }

                virtual Result DoOperateRange(void *dst, size_t dst_size, ams::fs::OperationId op_id, s64 offset, s64 size, const void *src, size_t src_size) override {
                    AMS_UNUSED(dst, dst_size, offset, size, src, src_size);
                    R_UNLESS(op_id == ams::fs::OperationId::Invalidate, ams::fs::ResultUnsupportedOperation());
                    R_SUCCEED();
                }
        };

        bool Accepts(ams::fs::OpenDirectoryMode mode, bool is_directory) {
            return is_directory ? (mode & ams::fs::OpenDirectoryMode_Directory) != 0 : (mode & ams::fs::OpenDirectoryMode_File) != 0;
        }

        /* A folder of a drive, read entry by entry; it fails once the drive is unplugged or mounted again. */
        class DriveDirectory final : public ams::fs::fsa::IDirectory, public ams::fs::impl::Newable {
            NON_COPYABLE(DriveDirectory);
            NON_MOVEABLE(DriveDirectory);
            private:
                DIR m_dir;
                char m_path[MaxFatFsPathLength];
                s32 m_drive;
                u32 m_generation;
                ams::fs::OpenDirectoryMode m_mode;
                bool m_is_open;
                bool m_is_empty;
            public:
                DriveDirectory() : m_dir(), m_path(), m_drive(0), m_generation(0), m_mode(), m_is_open(false), m_is_empty(false) { /* ... */ }

                virtual ~DriveDirectory() {
                    std::scoped_lock lk(GetVolumeLock());
                    if (this->IsValid()) {
                        f_closedir(std::addressof(m_dir));
                    }
                }

                Result Initialize(const Route &route, ams::fs::OpenDirectoryMode mode) {
                    std::scoped_lock lk(GetVolumeLock());
                    R_UNLESS(usbl::IsVolumeMounted(route.drive), usbl::ResultDriveDetached());

                    util::Strlcpy(m_path, route.fatfs_path, sizeof(m_path));
                    m_drive      = route.drive;
                    m_generation = usbl::GetVolumeGeneration(route.drive);
                    m_mode       = mode;

                    const FRESULT res = f_opendir(std::addressof(m_dir), m_path);
                    if (route.is_shared_root && res != FR_OK) {
                        /* No usbloadernx/usb folder on this drive: nothing shared. */
                        m_is_empty = true;
                        R_SUCCEED();
                    }
                    R_TRY(ConvertFatFsLookupResult(res));
                    m_is_open = true;
                    R_SUCCEED();
                }

                virtual sf::cmif::DomainObjectId GetDomainObjectId() const override {
                    return sf::cmif::InvalidDomainObjectId;
                }
            private:
                bool IsValid() const {
                    return m_is_open && usbl::IsVolumeMounted(m_drive) && usbl::GetVolumeGeneration(m_drive) == m_generation;
                }

                virtual Result DoRead(s64 *out_count, ams::fs::DirectoryEntry *out_entries, s64 max_entries) override {
                    if (m_is_empty) {
                        *out_count = 0;
                        R_SUCCEED();
                    }
                    std::scoped_lock lk(GetVolumeLock());
                    R_UNLESS(this->IsValid(), usbl::ResultDriveDetached());

                    s64 count = 0;
                    FILINFO info;
                    while (count < max_entries) {
                        R_TRY(usbl::ConvertFatFsResult(f_readdir(std::addressof(m_dir), std::addressof(info))));
                        if (info.fname[0] == '\x00') {
                            break;
                        }
                        const bool is_directory = (info.fattrib & AM_DIR) != 0;
                        if (!Accepts(m_mode, is_directory)) {
                            continue;
                        }
                        auto &entry = out_entries[count++];
                        std::memset(std::addressof(entry), 0, sizeof(entry));
                        util::Strlcpy(entry.name, info.fname, sizeof(entry.name));
                        entry.type = is_directory ? ams::fs::DirectoryEntryType_Directory : ams::fs::DirectoryEntryType_File;
                        entry.file_size = is_directory ? 0 : static_cast<s64>(info.fsize);
                    }
                    *out_count = count;
                    R_SUCCEED();
                }

                virtual Result DoGetEntryCount(s64 *out) override {
                    if (m_is_empty) {
                        *out = 0;
                        R_SUCCEED();
                    }
                    std::scoped_lock lk(GetVolumeLock());
                    R_UNLESS(this->IsValid(), usbl::ResultDriveDetached());

                    DIR dir;
                    R_TRY(ConvertFatFsLookupResult(f_opendir(std::addressof(dir), m_path)));
                    ON_SCOPE_EXIT { f_closedir(std::addressof(dir)); };

                    s64 count = 0;
                    FILINFO info;
                    while (f_readdir(std::addressof(dir), std::addressof(info)) == FR_OK && info.fname[0] != '\x00') {
                        count += Accepts(m_mode, (info.fattrib & AM_DIR) != 0) ? 1 : 0;
                    }
                    *out = count;
                    R_SUCCEED();
                }
        };

        /* /usbloadernx/usb: the drives mounted when it was opened. */
        class DriveListDirectory final : public ams::fs::fsa::IDirectory, public ams::fs::impl::Newable {
            NON_COPYABLE(DriveListDirectory);
            NON_MOVEABLE(DriveListDirectory);
            private:
                char m_names[usbl::MaxDrives][usbl::VolumeNameLength];
                s32 m_count;
                s32 m_next;
            public:
                explicit DriveListDirectory(ams::fs::OpenDirectoryMode mode) : m_names(), m_count(0), m_next(0) {
                    if ((mode & ams::fs::OpenDirectoryMode_Directory) == 0) {
                        return;
                    }
                    std::scoped_lock lk(GetVolumeLock());
                    s32 drives[usbl::MaxDrives];
                    m_count = usbl::ListMountedVolumes(drives, usbl::MaxDrives);
                    for (s32 i = 0; i < m_count; ++i) {
                        usbl::FormatVolumeName(m_names[i], sizeof(m_names[i]), drives[i]);
                    }
                }

                virtual sf::cmif::DomainObjectId GetDomainObjectId() const override {
                    return sf::cmif::InvalidDomainObjectId;
                }
            private:
                virtual Result DoRead(s64 *out_count, ams::fs::DirectoryEntry *out_entries, s64 max_entries) override {
                    s64 count = 0;
                    while (count < max_entries && m_next < m_count) {
                        auto &entry = out_entries[count++];
                        std::memset(std::addressof(entry), 0, sizeof(entry));
                        util::Strlcpy(entry.name, m_names[m_next++], sizeof(entry.name));
                        entry.type = ams::fs::DirectoryEntryType_Directory;
                    }
                    *out_count = count;
                    R_SUCCEED();
                }

                virtual Result DoGetEntryCount(s64 *out) override {
                    *out = m_count;
                    R_SUCCEED();
                }
        };

        /* /usbloadernx, which holds the usb folder (case does not matter, a trailing '/' either). */
        bool IsUsbParent(const ams::fs::Path &path) {
            const char *usb_dir = ::usbl::paths::HomebrewUsbDir;
            const size_t parent_length = static_cast<size_t>(std::strrchr(usb_dir, '/') - usb_dir);
            const char *str = path.GetString();
            return util::Strnicmp(str, usb_dir, static_cast<int>(parent_length)) == 0 && (str[parent_length] == '\x00' || (str[parent_length] == '/' && str[parent_length + 1] == '\x00'));
        }

        bool AnyDriveMounted() {
            std::scoped_lock lk(GetVolumeLock());
            s32 drives[usbl::MaxDrives];
            return usbl::ListMountedVolumes(drives, usbl::MaxDrives) > 0;
        }

        /* /usbloadernx as the SD card has it, plus the usb folder: it exists only while a drive is plugged in, nothing is
         * written on the SD card for it. */
        class UsbParentDirectory final : public ams::fs::fsa::IDirectory, public ams::fs::impl::Newable {
            NON_COPYABLE(UsbParentDirectory);
            NON_MOVEABLE(UsbParentDirectory);
            private:
                std::unique_ptr<ams::fs::fsa::IDirectory> m_base;
                bool m_extra_pending;
            public:
                explicit UsbParentDirectory(std::unique_ptr<ams::fs::fsa::IDirectory> &&base) : m_base(std::move(base)), m_extra_pending(true) { /* ... */ }

                virtual sf::cmif::DomainObjectId GetDomainObjectId() const override {
                    return sf::cmif::InvalidDomainObjectId;
                }
            private:
                virtual Result DoRead(s64 *out_count, ams::fs::DirectoryEntry *out_entries, s64 max_entries) override {
                    s64 count = 0;
                    R_TRY(m_base->Read(std::addressof(count), out_entries, max_entries));
                    if (count < max_entries && m_extra_pending) {
                        auto &entry = out_entries[count++];
                        std::memset(std::addressof(entry), 0, sizeof(entry));
                        util::Strlcpy(entry.name, std::strrchr(::usbl::paths::HomebrewUsbDir, '/') + 1, sizeof(entry.name));
                        entry.type = ams::fs::DirectoryEntryType_Directory;
                        m_extra_pending = false;
                    }
                    *out_count = count;
                    R_SUCCEED();
                }

                virtual Result DoGetEntryCount(s64 *out) override {
                    R_TRY(m_base->GetEntryCount(out));
                    *out += 1;
                    R_SUCCEED();
                }
        };

        Result ReadOnly() {
            R_THROW(ams::fs::ResultUnsupportedWriteForReadOnlyFileSystem());
        }

        /* Free and total space: measured at mount for a drive, nothing for the drive list. */
        Result GetDriveSpace(u64 *out_free, u64 *out_total, const Route &route) {
            *out_free = 0;
            *out_total = 0;
            R_SUCCEED_IF(route.target != Target::Drive);

            std::scoped_lock lk(GetVolumeLock());
            R_UNLESS(usbl::GetDriveSpace(route.drive, out_free, out_total), usbl::ResultDriveDetached());
            R_SUCCEED();
        }

    }

    UsblSdCardFileSystem::UsblSdCardFileSystem(std::unique_ptr<ams::fs::fsa::IFileSystem> &&base) : m_base(std::move(base)) {
        if (usbl::IsDebugEnabled()) {
            usbl::Log("fs.mitm: sd overlay %p created", static_cast<void *>(this));
        }
    }

    UsblSdCardFileSystem::~UsblSdCardFileSystem() {
        if (usbl::IsDebugEnabled()) {
            usbl::Log("fs.mitm: sd overlay %p closing", static_cast<void *>(this));
        }
        m_base.reset();
        if (usbl::IsDebugEnabled()) {
            usbl::Log("fs.mitm: sd overlay %p closed", static_cast<void *>(this));
        }
    }

    Result UsblSdCardFileSystem::DoGetEntryType(ams::fs::DirectoryEntryType *out, const ams::fs::Path &path) {
        Route route;
        R_TRY(Resolve(std::addressof(route), path));
        if (route.target == Target::SdCard) {
            R_RETURN(m_base->GetEntryType(out, path));
        }
        if (route.target == Target::DriveList) {
            *out = ams::fs::DirectoryEntryType_Directory;
            R_SUCCEED();
        }

        std::scoped_lock lk(GetVolumeLock());
        FILINFO info;
        R_TRY(StatOnDrive(std::addressof(info), route));
        *out = (info.fattrib & AM_DIR) != 0 ? ams::fs::DirectoryEntryType_Directory : ams::fs::DirectoryEntryType_File;
        R_SUCCEED();
    }

    Result UsblSdCardFileSystem::DoOpenFile(std::unique_ptr<ams::fs::fsa::IFile> *out_file, const ams::fs::Path &path, ams::fs::OpenMode mode) {
        Route route;
        R_TRY(Resolve(std::addressof(route), path));
        if (route.target == Target::SdCard) {
            R_RETURN(m_base->OpenFile(out_file, path, mode));
        }
        R_UNLESS((mode & (ams::fs::OpenMode_Write | ams::fs::OpenMode_AllowAppend)) == 0, ReadOnly());
        R_UNLESS(route.target == Target::Drive, ams::fs::ResultPathNotFound());

        s64 size;
        {
            std::scoped_lock lk(GetVolumeLock());
            FILINFO info;
            R_TRY(StatOnDrive(std::addressof(info), route));
            R_UNLESS((info.fattrib & AM_DIR) == 0, ams::fs::ResultPathNotFound());
            size = static_cast<s64>(info.fsize);
        }

        auto storage = std::make_unique<usbl::UsbFileStorage>();
        R_UNLESS(storage != nullptr, ams::fs::ResultAllocationMemoryFailedNew());
        R_TRY(storage->Initialize(route.drive, route.fatfs_path, 0, size));

        auto file = std::make_unique<DriveFile>(std::move(storage), size);
        R_UNLESS(file != nullptr, ams::fs::ResultAllocationMemoryFailedNew());
        *out_file = std::move(file);
        R_SUCCEED();
    }

    Result UsblSdCardFileSystem::DoOpenDirectory(std::unique_ptr<ams::fs::fsa::IDirectory> *out_dir, const ams::fs::Path &path, ams::fs::OpenDirectoryMode mode) {
        Route route;
        R_TRY(Resolve(std::addressof(route), path));
        if (route.target == Target::SdCard) {
            R_TRY(m_base->OpenDirectory(out_dir, path, mode));

            /* Lists the usb folder among the others, unless the SD card has a real one of that name. */
            ams::fs::DirectoryEntryType type;
            ams::fs::Path usb_path;
            if ((mode & ams::fs::OpenDirectoryMode_Directory) != 0 && IsUsbParent(path) && AnyDriveMounted() &&
                R_SUCCEEDED(ams::fs::SetUpFixedPath(std::addressof(usb_path), ::usbl::paths::HomebrewUsbDir)) && R_FAILED(m_base->GetEntryType(std::addressof(type), usb_path))) {
                auto dir = std::make_unique<UsbParentDirectory>(std::move(*out_dir));
                R_UNLESS(dir != nullptr, ams::fs::ResultAllocationMemoryFailedNew());
                *out_dir = std::move(dir);
            }
            R_SUCCEED();
        }
        if (route.target == Target::DriveList) {
            auto dir = std::make_unique<DriveListDirectory>(mode);
            R_UNLESS(dir != nullptr, ams::fs::ResultAllocationMemoryFailedNew());
            *out_dir = std::move(dir);
            R_SUCCEED();
        }

        auto dir = std::make_unique<DriveDirectory>();
        R_UNLESS(dir != nullptr, ams::fs::ResultAllocationMemoryFailedNew());
        R_TRY(dir->Initialize(route, mode));
        *out_dir = std::move(dir);
        R_SUCCEED();
    }

    Result UsblSdCardFileSystem::DoGetFileTimeStampRaw(ams::fs::FileTimeStampRaw *out, const ams::fs::Path &path) {
        Route route;
        R_TRY(Resolve(std::addressof(route), path));
        if (route.target == Target::SdCard) {
            R_RETURN(m_base->GetFileTimeStampRaw(out, path));
        }

        *out = {};
        R_SUCCEED_IF(route.target != Target::Drive);

        std::scoped_lock lk(GetVolumeLock());
        FILINFO info;
        R_TRY(StatOnDrive(std::addressof(info), route));
        const s64 time = ToPosixTime(info.fdate, info.ftime);
        out->create = time;
        out->modify = time;
        out->access = time;
        out->is_local_time = true;
        R_SUCCEED();
    }

    Result UsblSdCardFileSystem::DoGetFreeSpaceSize(s64 *out, const ams::fs::Path &path) {
        Route route;
        R_TRY(Resolve(std::addressof(route), path));
        if (route.target == Target::SdCard) {
            R_RETURN(m_base->GetFreeSpaceSize(out, path));
        }
        u64 free, total;
        R_TRY(GetDriveSpace(std::addressof(free), std::addressof(total), route));
        *out = static_cast<s64>(free);
        R_SUCCEED();
    }

    Result UsblSdCardFileSystem::DoGetTotalSpaceSize(s64 *out, const ams::fs::Path &path) {
        Route route;
        R_TRY(Resolve(std::addressof(route), path));
        if (route.target == Target::SdCard) {
            R_RETURN(m_base->GetTotalSpaceSize(out, path));
        }
        u64 free, total;
        R_TRY(GetDriveSpace(std::addressof(free), std::addressof(total), route));
        *out = static_cast<s64>(total);
        R_SUCCEED();
    }

    Result UsblSdCardFileSystem::DoCreateFile(const ams::fs::Path &path, s64 size, int flags) {
        R_UNLESS(!IsOnUsb(path), ReadOnly());
        R_RETURN(m_base->CreateFile(path, size, flags));
    }

    Result UsblSdCardFileSystem::DoDeleteFile(const ams::fs::Path &path) {
        R_UNLESS(!IsOnUsb(path), ReadOnly());
        R_RETURN(m_base->DeleteFile(path));
    }

    Result UsblSdCardFileSystem::DoCreateDirectory(const ams::fs::Path &path) {
        R_UNLESS(!IsOnUsb(path), ReadOnly());
        R_RETURN(m_base->CreateDirectory(path));
    }

    Result UsblSdCardFileSystem::DoDeleteDirectory(const ams::fs::Path &path) {
        R_UNLESS(!IsOnUsb(path), ReadOnly());
        R_RETURN(m_base->DeleteDirectory(path));
    }

    Result UsblSdCardFileSystem::DoDeleteDirectoryRecursively(const ams::fs::Path &path) {
        R_UNLESS(!IsOnUsb(path), ReadOnly());
        R_RETURN(m_base->DeleteDirectoryRecursively(path));
    }

    Result UsblSdCardFileSystem::DoCleanDirectoryRecursively(const ams::fs::Path &path) {
        R_UNLESS(!IsOnUsb(path), ReadOnly());
        R_RETURN(m_base->CleanDirectoryRecursively(path));
    }

    Result UsblSdCardFileSystem::DoRenameFile(const ams::fs::Path &old_path, const ams::fs::Path &new_path) {
        R_UNLESS(!IsOnUsb(old_path) && !IsOnUsb(new_path), ReadOnly());
        R_RETURN(m_base->RenameFile(old_path, new_path));
    }

    Result UsblSdCardFileSystem::DoRenameDirectory(const ams::fs::Path &old_path, const ams::fs::Path &new_path) {
        R_UNLESS(!IsOnUsb(old_path) && !IsOnUsb(new_path), ReadOnly());
        R_RETURN(m_base->RenameDirectory(old_path, new_path));
    }

    Result UsblSdCardFileSystem::DoQueryEntry(char *dst, size_t dst_size, const char *src, size_t src_size, ams::fs::fsa::QueryId query, const ams::fs::Path &path) {
        R_UNLESS(!IsOnUsb(path), ams::fs::ResultUnsupportedOperation());
        R_RETURN(m_base->QueryEntry(dst, dst_size, src, src_size, query, path));
    }

    Result UsblSdCardFileSystem::DoCommit()                         { R_RETURN(m_base->Commit()); }
    Result UsblSdCardFileSystem::DoCommitProvisionally(s64 counter) { R_RETURN(m_base->CommitProvisionally(counter)); }
    Result UsblSdCardFileSystem::DoRollback()                       { R_RETURN(m_base->Rollback()); }
    Result UsblSdCardFileSystem::DoFlush()                          { R_RETURN(m_base->Flush()); }

}
