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
#include "usbl_usb_volume_manager.hpp"
#include "usb/usb_host.hpp"
#include "usb/usb_mass_storage_device.hpp"
#include "storage/usbl_volume.hpp"
#include "content/usbl_content_index.hpp"
#include "content/usbl_content_meta.hpp"
#include "content/usbl_known_contents.hpp"
#include "content/usbl_registration.hpp"
#include "content/usbl_game_indexer.hpp"
#include "usbl_config.hpp"
#include "usbl_disk_io.hpp"
#include "usbl_log.hpp"
#include "usbl_perf.hpp"
#include "usbl_notifications.hpp"
#include "usbl_power_state.hpp"
#include "usbl_results.hpp"

extern "C" {
#include "fatfs/ff.h"
}

namespace ams::mitm::usbl {

    namespace {

        /* Only this folder (and two levels of subfolders) of each drive is scanned, so other files cost nothing. */
        constexpr const char GamesFolderPath[] = "/usbloadernx/games";
        constexpr size_t VolumePathLength = 4;
        constexpr int MaxFolderDepth = 2;

        constexpr size_t MaxListedEntries = 4096;
        constexpr size_t MaxPathLength    = 0x300;
        constexpr s32 MaxQueriedInterfaces = 8;

        constexpr size_t ThreadStackSize = 64_KB;
        alignas(os::ThreadStackAlignment) constinit u8 g_thread_stack[ThreadStackSize];
        constinit os::ThreadType g_thread;

        /* What tells the same drive apart across a sleep, when it re-enumerates with a new interface id. */
        struct DriveIdentity {
            u16 vendor_id;
            u16 product_id;
            u64 block_count;

            constexpr bool operator==(const DriveIdentity &) const = default;
        };

        struct DriveSlot {
            usb::MassStorageDevice device;
            FATFS fatfs;
            DriveIdentity identity;
            s32 interface_id;
            bool active;
            bool mounted;
            /* What the Manager shows, from the moment the drive is opened; written and read under g_status_lock. */
            DriveStatus status;
            bool is_listed;
        };

        /* Slot index == FatFs volume number. Only this thread changes slots; FatFs calls still take the volume lock. */
        constinit DriveSlot g_slots[MaxDrives] = {};

        /* Guards the slots' status apart from the volume lock, which mounting and indexing hold for as long as they take:
         * the Manager can list a drive that is still being read. Taken inside the volume lock, never around it. */
        constinit os::SdkMutex g_status_lock;

        /* Drives that dropped off the bus because the console went to sleep; their return is not announced. */
        constinit DriveIdentity g_sleeping_drives[MaxDrives] = {};
        constinit s32 g_sleeping_drive_count = 0;

        void RememberSleepingDrive(const DriveIdentity &identity) {
            if (g_sleeping_drive_count < MaxDrives) {
                g_sleeping_drives[g_sleeping_drive_count++] = identity;
            }
        }

        bool TakeSleepingDrive(const DriveIdentity &identity) {
            const auto it = std::find(g_sleeping_drives, g_sleeping_drives + g_sleeping_drive_count, identity);
            if (it == g_sleeping_drives + g_sleeping_drive_count) {
                return false;
            }

            *it = g_sleeping_drives[--g_sleeping_drive_count];
            return true;
        }

        /* Before the drive answers there is no block count yet: a drive of the same make waking with the console is
         * taken for one coming back from sleep, and not announced. */
        bool MaybeSleepingDrive(const ::UsbHsInterface &intf) {
            return std::any_of(g_sleeping_drives, g_sleeping_drives + g_sleeping_drive_count, [&](const DriveIdentity &identity) {
                return identity.vendor_id == intf.device_desc.idVendor && identity.product_id == intf.device_desc.idProduct;
            });
        }

        /* Interfaces that failed to open stay "available"; skip them until they are unplugged. */
        constinit s32 g_rejected_interfaces[MaxQueriedInterfaces] = {};
        constinit s32 g_rejected_count = 0;

        constinit ::UsbHsInterface g_available_interfaces[MaxQueriedInterfaces] = {};

        /* One per folder level being read. */
        constinit FILINFO g_file_infos[MaxFolderDepth + 1] = {};

        void FormatVolumePath(char *dst, size_t dst_size, s32 drive) {
            util::SNPrintf(dst, dst_size, "%d:", drive);
        }

        /* Case-insensitive match of a 3-letter extension such as "xci". */
        bool HasExtension(const char *name, const char *ext) {
            const size_t len = std::strlen(name);
            if (len < 4 || name[len - 4] != '.') {
                return false;
            }

            for (int i = 0; i < 3; ++i) {
                if ((name[len - 3 + i] | 0x20) != ext[i]) {
                    return false;
                }
            }
            return true;
        }

        struct GameFileCount {
            size_t games;
            size_t updates;
            size_t add_ons;
            /* Dumps and folders not read: path too long, folder too deep, too many entries, index full. */
            size_t left_out;
        };

        bool IsDumpName(const char *name) {
            return HasExtension(name, "xci") || HasExtension(name, "nsp");
        }

        void IndexGameFolder(s32 drive, const char *folder, int depth, GameFileCount *count) {
            DIR dir;
            if (const FRESULT res = f_opendir(std::addressof(dir), folder); res != FR_OK) {
                Log("cannot open %s: %d", folder, static_cast<int>(res));
                return;
            }
            ON_SCOPE_EXIT { f_closedir(std::addressof(dir)); };

            FILINFO &info = g_file_infos[depth];
            size_t entries = 0;
            for (; entries < MaxListedEntries && f_readdir(std::addressof(dir), std::addressof(info)) == FR_OK && info.fname[0] != '\x00'; ++entries) {
                const bool is_folder = (info.fattrib & AM_DIR) != 0;
                char path[MaxPathLength];
                if (static_cast<size_t>(util::SNPrintf(path, sizeof(path), "%s/%s", folder, info.fname)) >= sizeof(path)) {
                    if (is_folder || IsDumpName(info.fname)) {
                        Log("left out %s/%s: the path is longer than %zu bytes", folder, info.fname, MaxPathLength - 1);
                        ++count->left_out;
                    }
                    continue;
                }

                if (is_folder) {
                    if (depth < MaxFolderDepth) {
                        IndexGameFolder(drive, path, depth + 1, count);
                    } else {
                        Log("left out %s: only two levels of folders inside games are read", path);
                        ++count->left_out;
                    }
                } else if (HasExtension(info.fname, "xci")) {
                    ++count->games;
                    content::IndexXci(drive, path);
                } else if (HasExtension(info.fname, "nsp")) {
                    switch (content::IndexNsp(drive, path)) {
                        case content::NspKind::Application:  ++count->games;   break;
                        case content::NspKind::Patch:        ++count->updates; break;
                        case content::NspKind::AddOnContent: ++count->add_ons; break;
                        case content::NspKind::Unknown:                        break;
                    }
                }
            }

            if (entries == MaxListedEntries && f_readdir(std::addressof(dir), std::addressof(info)) == FR_OK && info.fname[0] != '\x00') {
                Log("left out: %s has more than %zu entries, the rest is not read", folder, MaxListedEntries);
                ++count->left_out;
            }
        }

        /* nullopt when the drive has no games folder. */
        util::optional<GameFileCount> IndexGames(s32 drive) {
            char folder[VolumePathLength + sizeof(GamesFolderPath)];
            util::SNPrintf(folder, sizeof(folder), "%d:%s", drive, GamesFolderPath);

            FILINFO folder_info;
            if (f_stat(folder, std::addressof(folder_info)) != FR_OK || (folder_info.fattrib & AM_DIR) == 0) {
                Log("drive %d: no %s folder", drive, GamesFolderPath);
                return util::nullopt;
            }

            GameFileCount count = {};
            R_DISCARD(TakeIndexFullCount());
            IndexGameFolder(drive, folder, 0, std::addressof(count));
            if (const s32 full = TakeIndexFullCount(); full > 0) {
                Log("left out %d files: no memory left for the index", full);
                count.left_out += full;
            }
            Log("drive %d: %zu games, %zu updates, %zu add-ons, %d contents indexed in total", drive, count.games, count.updates, count.add_ons, GetIndexedContentCount());
            if (count.left_out > 0) {
                Log("drive %d: %zu files or folders left out (see the lines above)", drive, count.left_out);
            }
            return count;
        }

        void LogDeviceInfo(s32 drive, const ::UsbHsInterface &intf) {
            Log("drive %d: interface %d vid=%04x pid=%04x path=%s", drive, intf.inf.ID, intf.device_desc.idVendor, intf.device_desc.idProduct, intf.pathstr);
        }

        void LogDriveInfo(s32 drive) {
            auto &device = g_slots[drive].device;

            usb::InquiryData inquiry = {};
            if (R_SUCCEEDED(device.Inquiry(std::addressof(inquiry)))) {
                Log("drive %d: '%s' '%s' rev '%s'", drive, inquiry.vendor, inquiry.product, inquiry.revision);
                std::scoped_lock lk(g_status_lock);
                util::SNPrintf(g_slots[drive].status.name, sizeof(g_slots[drive].status.name), "%s %s", inquiry.vendor, inquiry.product);
            }

            const u64 size_mb = (device.GetBlockCount() * device.GetBlockSize()) / 1_MB;
            Log("drive %d: %" PRIu64 " blocks x %u bytes (%" PRIu64 " MB)", drive, device.GetBlockCount(), device.GetBlockSize(), size_mb);
        }

        DriveState MountAndIndex(s32 drive, GameFileCount *out_count) {
            std::scoped_lock lk(GetVolumeLock());

            char volume[VolumePathLength];
            FormatVolumePath(volume, sizeof(volume), drive);

            BindDiskDevice(drive, std::addressof(g_slots[drive].device));
            if (const FRESULT res = f_mount(std::addressof(g_slots[drive].fatfs), volume, 1); res != FR_OK) {
                Log("drive %d: f_mount failed: %d", drive, static_cast<int>(res));
                UnbindDiskDevice(drive);
                return DriveState::Unreadable;
            }

            NotifyVolumeMounted(drive);
            const FATFS &fatfs = g_slots[drive].fatfs;
            const bool is_exfat = fatfs.fs_type == FS_EXFAT;
            {
                /* The mounted volume's size, not the whole drive's: a drive can have other partitions next to it. */
                std::scoped_lock status_lk(g_status_lock);
                g_slots[drive].status.is_exfat = is_exfat;
                g_slots[drive].status.capacity = static_cast<u64>(fatfs.n_fatent - 2) * fatfs.csize * g_slots[drive].device.GetBlockSize();
                g_slots[drive].status.is_counting_free_space = true;
            }
            Log("drive %d: mounted %s volume", drive, is_exfat ? "exFAT" : "FAT");

            /* Names the drive for homebrew (sdmc:/usbloadernx/usb/<serial>/): the same at every boot and in any port. */
            {
                char label[64];
                DWORD serial = 0;
                if (f_getlabel(volume, label, std::addressof(serial)) != FR_OK) {
                    serial = 0;
                }
                SetVolumeSerial(drive, serial);
                {
                    std::scoped_lock status_lk(g_status_lock);
                    g_slots[drive].status.volume_serial = serial;
                }

                char name[VolumeNameLength];
                FormatVolumeName(name, sizeof(name), drive);
                Log("drive %d: homebrew see it at sdmc:/usbloadernx/usb/%s/", drive, name);
            }

            ClearContentIndex(drive);
            const auto games = IndexGames(drive);
            if (!games.has_value()) {
                return DriveState::NoGamesFolder;
            }

            *out_count = *games;
            return DriveState::Ready;
        }

        /* For the Manager's usage bar, once the games are on HOME: FSInfo on FAT32 (one sector), the allocation bitmap on
         * exFAT; only a FAT32 volume with no valid FSInfo needs a pass over the whole FAT. Reads only. */
        void CountFreeSpace(s32 drive) {
            std::scoped_lock lk(GetVolumeLock());

            char volume[VolumePathLength];
            FormatVolumePath(volume, sizeof(volume), drive);

            DWORD free_clusters = 0;
            FATFS *fatfs = nullptr;
            const bool counted = f_countfree(volume, std::addressof(free_clusters), std::addressof(fatfs)) == FR_OK;

            u64 free_space = 0, capacity = 0;
            {
                std::scoped_lock status_lk(g_status_lock);
                if (counted) {
                    g_slots[drive].status.free_space = static_cast<u64>(free_clusters) * fatfs->csize * g_slots[drive].device.GetBlockSize();
                }
                g_slots[drive].status.is_counting_free_space = false;
                free_space = g_slots[drive].status.free_space;
                capacity   = g_slots[drive].status.capacity;
            }
            if (counted) {
                Log("drive %d: %" PRIu64 " MB free of %" PRIu64 " MB", drive, free_space / 1_MB, capacity / 1_MB);
            } else {
                Log("drive %d: free space not counted", drive);
            }
        }

        void Unmount(s32 drive) {
            std::scoped_lock lk(GetVolumeLock());

            char volume[VolumePathLength];
            FormatVolumePath(volume, sizeof(volume), drive);

            ClearContentIndex(drive);
            NotifyVolumeUnmounted(drive);
            f_mount(nullptr, volume, 0);
            UnbindDiskDevice(drive);
        }

        bool IsRejected(s32 interface_id) {
            return std::find(g_rejected_interfaces, g_rejected_interfaces + g_rejected_count, interface_id) != g_rejected_interfaces + g_rejected_count;
        }

        void Reject(s32 interface_id) {
            if (g_rejected_count < MaxQueriedInterfaces) {
                g_rejected_interfaces[g_rejected_count++] = interface_id;
            }
        }

        /* Forgets rejected interfaces that are no longer plugged in. */
        void PruneRejected(const ::UsbHsInterface *available, s32 available_count) {
            g_rejected_count = static_cast<s32>(std::remove_if(g_rejected_interfaces, g_rejected_interfaces + g_rejected_count, [&](s32 id) {
                return std::none_of(available, available + available_count, [id](const ::UsbHsInterface &intf) { return intf.inf.ID == id; });
            }) - g_rejected_interfaces);
        }

        void AttachDrive(s32 drive, ::UsbHsInterface &intf) {
            DriveSlot &slot = g_slots[drive];
            LogDeviceInfo(drive, intf);

            /* Said before opening: a hard drive can take up to UnitReadyTimeout to spin up. */
            const bool maybe_resuming = MaybeSleepingDrive(intf);
            if (!maybe_resuming) {
                NotifyDriveReading();
            }

            if (const Result result = slot.device.Open(std::addressof(intf)); R_FAILED(result)) {
                Log("drive %d: failed to open: 2%03d-%04d", drive, result.GetModule(), result.GetDescription());
                Reject(intf.inf.ID);
                if (!ResultDriveDetached::Includes(result)) {
                    NotifyDriveNotOpened();
                }
                return;
            }

            slot.interface_id = slot.device.GetInterfaceId();
            slot.identity     = { intf.device_desc.idVendor, intf.device_desc.idProduct, slot.device.GetBlockCount() };
            slot.active       = true;
            const bool resumed = TakeSleepingDrive(slot.identity);

            /* Listed right away: a large drive can take a while to mount and index. */
            {
                std::scoped_lock lk(g_status_lock);
                slot.status    = { .drive = drive, .state = DriveState::Reading, .capacity = slot.device.GetBlockCount() * slot.device.GetBlockSize() };
                slot.is_listed = true;
            }

            LogDriveInfo(drive);
            GameFileCount game_files = {};
            DriveState state;
            {
                perf::ScopedStep step("mounted and indexed", drive);
                state = MountAndIndex(drive, std::addressof(game_files));
            }
            slot.mounted = state != DriveState::Unreadable;
            {
                std::scoped_lock lk(g_status_lock);
                slot.status.state        = state;
                slot.status.game_count   = game_files.games;
                slot.status.update_count = game_files.updates;
                slot.status.add_on_count = game_files.add_ons;
            }

            if (resumed) {
                Log("drive %d: back after sleep", drive);
            } else if (state == DriveState::Unreadable) {
                NotifyDriveUnreadable(slot.status.name);
            } else {
                NotifyDriveConnected({
                    .name             = slot.status.name,
                    .capacity         = slot.status.capacity,
                    .is_exfat         = slot.status.is_exfat,
                    .has_games_folder = state == DriveState::Ready,
                    .volume_serial    = slot.status.volume_serial,
                    .games            = game_files.games,
                    .updates          = game_files.updates,
                    .add_ons          = game_files.add_ons,
                    .left_out         = game_files.left_out,
                });
            }

            switch (state) {
                case DriveState::Ready:
                    content::ExcludeInstalledApplications();
                    if (IsDebugEnabled()) {
                        content::SelfTestContentIndex();
                        content::LogIndexedContentMetas();
                    }
                    RememberIndexedContents();
                    perf::LogHeap("after indexing");
                    {
                        perf::ScopedStep step("registered", drive);
                        content::RegisterIndexedApplicationsIfEnabled();
                    }
                    if (IsConfigEnabled(ConfigKey::AutoRegister)) {
                        ForgetDeletedGameParts();
                    }
                    break;
                case DriveState::NoGamesFolder:
                case DriveState::Unreadable:
                case DriveState::Reading:
                    break;
            }

            if (slot.mounted) {
                CountFreeSpace(drive);
            }
        }

        constexpr s32 MaxApplicationsPerDrive = 64;
        constexpr TimeSpan BootDriveGracePeriod = TimeSpan::FromSeconds(15);

        void DetachDrive(s32 drive) {
            DriveSlot &slot = g_slots[drive];
            const bool by_sleep = IsInSleepTransition();
            if (by_sleep) {
                Log("drive %d: detached by sleep", drive);
                RememberSleepingDrive(slot.identity);
            } else {
                Log("drive %d: detached", drive);
                NotifyDriveRemoved(slot.status.name);
            }

            ncm::ProgramId applications[MaxApplicationsPerDrive];
            s32 application_count = 0;
            {
                std::scoped_lock lk(GetVolumeLock());
                application_count = GetIndexedApplications(applications, MaxApplicationsPerDrive, drive);
            }

            if (slot.mounted) {
                Unmount(drive);
            }

            {
                std::scoped_lock lk(GetVolumeLock());
                slot.device.Close();
                slot.active    = false;
                slot.mounted   = false;
                std::scoped_lock status_lk(g_status_lock);
                slot.is_listed = false;
            }

            /* A drive the console unpowers for sleep comes right back; its games stay on HOME. */
            if (!by_sleep) {
                content::HideUnpluggedApplications(applications, application_count);
            }
        }

        void DetachRemovedDrives() {
            for (s32 drive = 0; drive < MaxDrives; ++drive) {
                if (g_slots[drive].active && !usb::IsInterfaceAcquired(g_slots[drive].interface_id)) {
                    DetachDrive(drive);
                }
            }
        }

        void AttachNewDrives() {
            const s32 count = usb::QueryAvailableInterfaces(g_available_interfaces, MaxQueriedInterfaces);
            PruneRejected(g_available_interfaces, count);

            for (s32 i = 0; i < count; ++i) {
                ::UsbHsInterface &intf = g_available_interfaces[i];
                if (IsRejected(intf.inf.ID)) {
                    continue;
                }

                const auto free_slot = std::find_if(g_slots, g_slots + MaxDrives, [](const DriveSlot &slot) { return !slot.active; });
                if (free_slot == g_slots + MaxDrives) {
                    Log("interface %d: ignored, %d drives already in use", intf.inf.ID, MaxDrives);
                    Reject(intf.inf.ID);
                    continue;
                }

                AttachDrive(static_cast<s32>(free_slot - g_slots), intf);
            }
        }

        void UsbVolumeThreadFunction(void *) {
            if (const Result result = usb::Initialize(); R_FAILED(result)) {
                Log("usb initialization failed: 2%03d-%04d", result.GetModule(), result.GetDescription());
                return;
            }

            Log("usb: host ready");

            /* Drives already plugged in at boot take a moment to enumerate; only hide what is still missing after that.
             * Counted from now, so it runs alongside the wait for the SD card ncm database below instead of after it. */
            const os::Tick boot_hide_tick = os::GetSystemTick() + os::ConvertToTick(BootDriveGracePeriod);
            bool boot_hide_pending = true;

            /* Titles installed since their drive was last plugged in. */
            {
                perf::ScopedStep step("boot: installed titles checked", -1);
                content::ExcludeInstalledApplications();
            }

            while (true) {
                DetachRemovedDrives();
                AttachNewDrives();

                if (boot_hide_pending && os::GetSystemTick() >= boot_hide_tick) {
                    boot_hide_pending = false;
                    Log("boot: putting HOME in line with the options");
                    content::ApplyHomeVisibility();
                    /* ns deleted the contents of games without a record (hidden ones) while it finished booting; they are
                     * still on the drive, so they are known again. */
                    RememberIndexedContents();
                }

                if (boot_hide_pending) {
                    usb::WaitForEvent((boot_hide_tick - os::GetSystemTick()).ToTimeSpan());
                } else {
                    usb::WaitForEvent();
                }
            }
        }

    }

    void StartUsbVolumeManager() {
        R_ABORT_UNLESS(os::CreateThread(std::addressof(g_thread), UsbVolumeThreadFunction, nullptr, g_thread_stack, sizeof(g_thread_stack), AMS_GET_SYSTEM_THREAD_PRIORITY(mitm_usbl, UsbVolume)));
        os::SetThreadNamePointer(std::addressof(g_thread), AMS_GET_SYSTEM_THREAD_NAME(mitm_usbl, UsbVolume));
        os::StartThread(std::addressof(g_thread));
    }

    s32 GetDriveStatuses(DriveStatus *out, s32 max_count) {
        std::scoped_lock lk(g_status_lock);

        s32 count = 0;
        for (s32 drive = 0; drive < MaxDrives && count < max_count; ++drive) {
            if (g_slots[drive].is_listed) {
                out[count++] = g_slots[drive].status;
            }
        }
        return count;
    }

    bool GetDriveSpace(s32 drive, u64 *out_free, u64 *out_total) {
        AMS_ASSERT(GetVolumeLock().IsLockedByCurrentThread());
        if (drive < 0 || drive >= MaxDrives || !g_slots[drive].mounted) {
            return false;
        }
        std::scoped_lock status_lk(g_status_lock);
        *out_free  = g_slots[drive].status.free_space;
        *out_total = g_slots[drive].status.capacity;
        return true;
    }

}
