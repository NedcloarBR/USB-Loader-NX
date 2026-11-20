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
#include "usbl_usb_file_storage.hpp"
#include "usbl_volume.hpp"
#include "../usbl_results.hpp"
#include "../usbl_perf.hpp"
#include "../usbl_disk_io.hpp"

namespace ams::mitm::usbl {

    namespace {

        /* A link map takes two entries per fragment plus one; a dump copied onto a fuller drive can be split in many. */
        constexpr size_t InitialLinkMapEntries = 0x40;
        constexpr size_t MaxLinkMapEntries     = 0x2000;

    }

    Result ConvertFatFsResult(FRESULT res) {
        R_UNLESS(res == FR_OK, usbl::ResultFatFsError());
        R_SUCCEED();
    }

    namespace {

        Result ReadWithFatFs(FIL *file, u64 offset, void *dst, size_t size) {
            R_TRY(ConvertFatFsResult(f_lseek(file, static_cast<FSIZE_t>(offset))));

            UINT read = 0;
            R_TRY(ConvertFatFsResult(f_read(file, dst, static_cast<UINT>(size), std::addressof(read))));
            R_UNLESS(read == size, usbl::ResultShortTransfer());
            R_SUCCEED();
        }

        /* The sector holding a byte of a file with a link map, and how many bytes stay contiguous on the drive from there. */
        bool MapFileOffset(const FIL *file, u64 offset, u64 *out_sector, u64 *out_run) {
            const FATFS *fs = file->obj.fs;
            const u64 cluster_size = static_cast<u64>(fs->csize) * fs->ssize;
            const u64 within = offset % cluster_size;
            u64 cluster = offset / cluster_size;

            /* After the used length: one (cluster count, first cluster) pair per fragment, then 0. */
            for (const DWORD *entry = file->cltbl + 1; entry[0] != 0; entry += 2) {
                if (cluster < entry[0]) {
                    *out_sector = fs->database + static_cast<u64>(fs->csize) * (entry[1] + cluster - 2) + within / fs->ssize;
                    *out_run    = (entry[0] - cluster) * cluster_size - within;
                    return true;
                }
                cluster -= entry[0];
            }
            return false;
        }

    }

    Result ReadFileAt(FIL *file, s64 offset, void *dst, size_t size) {
        AMS_ASSERT(GetVolumeLock().IsLockedByCurrentThread());

        /* FatFs reads at most a cluster per device command: with a link map, whole sectors are read a fragment at a time. */
        if (file->cltbl == nullptr || static_cast<u64>(offset) + size > file->obj.objsize) {
            R_RETURN(ReadWithFatFs(file, offset, dst, size));
        }

        const u32 sector_size = file->obj.fs->ssize;
        u8 *cur = static_cast<u8 *>(dst);
        u64 pos = offset;
        size_t remaining = size;

        if (const size_t misalignment = pos % sector_size; misalignment != 0) {
            const size_t head = std::min<size_t>(remaining, sector_size - misalignment);
            R_TRY(ReadWithFatFs(file, pos, cur, head));
            cur += head; pos += head; remaining -= head;
        }

        while (remaining >= sector_size) {
            u64 sector, run;
            if (!MapFileOffset(file, pos, std::addressof(sector), std::addressof(run))) {
                break;
            }
            const size_t chunk = std::min<u64>(run, remaining) / sector_size * sector_size;
            R_TRY(ReadDiskSectors(file->obj.fs->pdrv, cur, sector, static_cast<u32>(chunk / sector_size)));
            cur += chunk; pos += chunk; remaining -= chunk;
        }

        if (remaining > 0) {
            R_TRY(ReadWithFatFs(file, pos, cur, remaining));
        }
        R_SUCCEED();
    }

    namespace {

        /* A fragmented file's map costs a walk of its FAT chain, sector by sector: the same file is opened once per
         * content it holds, so each map is kept for the mount it was built in. */
        struct CachedLinkMap {
            s32 drive;
            u32 generation;
            DWORD first_cluster;
            FSIZE_t size;
            std::unique_ptr<DWORD[]> table;
        };

        constexpr size_t MaxCachedLinkMaps = 32;
        constinit CachedLinkMap g_link_maps[MaxCachedLinkMaps] = {};
        constinit size_t g_next_link_map = 0;

        bool IsSameFile(const CachedLinkMap &cached, const FIL *file) {
            const s32 drive = file->obj.fs->pdrv;
            return cached.table != nullptr && cached.drive == drive && cached.generation == GetVolumeGeneration(drive) &&
                   cached.first_cluster == file->obj.sclust && cached.size == file->obj.objsize;
        }

        std::unique_ptr<DWORD[]> CopyLinkMap(const DWORD *table) {
            const size_t used = table[0];
            auto copy = std::unique_ptr<DWORD[]>(new (std::nothrow) DWORD[used]);
            if (copy != nullptr) {
                std::memcpy(copy.get(), table, used * sizeof(DWORD));
            }
            return copy;
        }

        std::unique_ptr<DWORD[]> BuildLinkMap(FIL *file) {
            /* Without it, every backward seek in a fragmented file walks the cluster chain from the start. */
            size_t entries = InitialLinkMapEntries;
            while (entries <= MaxLinkMapEntries) {
                auto link_map = std::unique_ptr<DWORD[]>(new (std::nothrow) DWORD[entries]);
                if (link_map == nullptr) {
                    return nullptr;
                }

                link_map[0] = static_cast<DWORD>(entries);
                file->cltbl = link_map.get();
                const FRESULT res = f_lseek(file, CREATE_LINKMAP);
                if (res == FR_OK) {
                    return link_map;
                }

                file->cltbl = nullptr;
                if (res != FR_NOT_ENOUGH_CORE) {
                    return nullptr;
                }
                entries = link_map[0];
            }
            return nullptr;
        }

    }

    std::unique_ptr<DWORD[]> CreateLinkMap(FIL *file) {
        AMS_ASSERT(GetVolumeLock().IsLockedByCurrentThread());

        for (const auto &cached : g_link_maps) {
            if (IsSameFile(cached, file)) {
                auto copy = CopyLinkMap(cached.table.get());
                file->cltbl = copy.get();
                return copy;
            }
        }

        auto link_map = BuildLinkMap(file);
        if (link_map != nullptr) {
            CachedLinkMap &slot = g_link_maps[g_next_link_map];
            g_next_link_map = (g_next_link_map + 1) % MaxCachedLinkMaps;
            slot = { file->obj.fs->pdrv, GetVolumeGeneration(file->obj.fs->pdrv), file->obj.sclust, file->obj.objsize, CopyLinkMap(link_map.get()) };
        }
        return link_map;
    }

    s32 CountFileFragments(FIL *file) {
        const auto link_map = CreateLinkMap(file);
        file->cltbl = nullptr;

        /* On success FatFs stores the used length: one leading entry, then a (length, cluster) pair per fragment. */
        return link_map != nullptr ? static_cast<s32>((link_map[0] - 1) / 2) : -1;
    }

    UsbFileStorage::~UsbFileStorage() {
        std::scoped_lock lk(GetVolumeLock());

        /* A handle from a previous mount must not be touched: its FATFS object was reused. */
        if (this->IsHandleValid()) {
            f_close(std::addressof(m_file));
        }
    }

    Result UsbFileStorage::Initialize(s32 drive, const char *path, s64 offset, s64 size) {
        AMS_ASSERT(!m_is_open);
        std::scoped_lock lk(GetVolumeLock());

        R_UNLESS(IsVolumeMounted(drive), usbl::ResultDriveDetached());
        R_TRY(ConvertFatFsResult(f_open(std::addressof(m_file), path, FA_READ)));
        m_link_map = CreateLinkMap(std::addressof(m_file));

        m_drive      = drive;
        m_offset     = offset;
        m_size       = size;
        m_generation = GetVolumeGeneration(drive);
        m_is_open    = true;
        R_SUCCEED();
    }

    bool UsbFileStorage::IsHandleValid() const {
        return m_is_open && IsVolumeMounted(m_drive) && GetVolumeGeneration(m_drive) == m_generation;
    }

    Result UsbFileStorage::Read(s64 offset, void *buffer, size_t size) {
        R_SUCCEED_IF(size == 0);
        R_UNLESS(buffer != nullptr, fs::ResultNullptrArgument());
        R_TRY(IStorage::CheckAccessRange(offset, size, m_size));

        const os::Tick start = os::GetSystemTick();
        std::scoped_lock lk(GetVolumeLock());
        const os::Tick locked = os::GetSystemTick();
        R_UNLESS(this->IsHandleValid(), usbl::ResultDriveDetached());

        R_TRY(ReadFileAt(std::addressof(m_file), m_offset + offset, buffer, size));
        perf::Record(perf::Layer::File, size, (os::GetSystemTick() - start).ToTimeSpan(), (locked - start).ToTimeSpan());
        R_SUCCEED();
    }

    Result UsbFileStorage::GetSize(s64 *out) {
        *out = m_size;
        R_SUCCEED();
    }

    Result UsbFileStorage::Write(s64 offset, const void *buffer, size_t size) {
        AMS_UNUSED(offset, buffer, size);
        R_THROW(fs::ResultUnsupportedOperation());
    }

    Result UsbFileStorage::Flush() {
        R_SUCCEED();
    }

    Result UsbFileStorage::SetSize(s64 size) {
        AMS_UNUSED(size);
        R_THROW(fs::ResultUnsupportedOperation());
    }

    Result UsbFileStorage::OperateRange(void *dst, size_t dst_size, fs::OperationId op_id, s64 offset, s64 size, const void *src, size_t src_size) {
        AMS_UNUSED(src, src_size);

        switch (op_id) {
            case fs::OperationId::Invalidate:
                R_SUCCEED();
            case fs::OperationId::QueryRange:
                R_UNLESS(dst != nullptr,                         fs::ResultNullptrArgument());
                R_UNLESS(dst_size == sizeof(fs::QueryRangeInfo), fs::ResultInvalidSize());
                R_TRY(IStorage::CheckOffsetAndSize(offset, size));
                reinterpret_cast<fs::QueryRangeInfo *>(dst)->Clear();
                R_SUCCEED();
            default:
                R_THROW(fs::ResultUnsupportedOperateRangeForFileStorage());
        }
    }

}
