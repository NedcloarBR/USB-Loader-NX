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

    Result ReadFileAt(FIL *file, s64 offset, void *dst, size_t size) {
        AMS_ASSERT(GetVolumeLock().IsLockedByCurrentThread());

        R_TRY(ConvertFatFsResult(f_lseek(file, static_cast<FSIZE_t>(offset))));

        UINT read = 0;
        R_TRY(ConvertFatFsResult(f_read(file, dst, static_cast<UINT>(size), std::addressof(read))));
        R_UNLESS(read == size, usbl::ResultShortTransfer());
        R_SUCCEED();
    }

    std::unique_ptr<DWORD[]> CreateLinkMap(FIL *file) {
        AMS_ASSERT(GetVolumeLock().IsLockedByCurrentThread());

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

        std::scoped_lock lk(GetVolumeLock());
        R_UNLESS(this->IsHandleValid(), usbl::ResultDriveDetached());

        R_RETURN(ReadFileAt(std::addressof(m_file), m_offset + offset, buffer, size));
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
