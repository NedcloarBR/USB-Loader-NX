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

    /* USB Loader NX: the SD card content storage, where NCAs served from USB also exist as read-only files (raw, still encrypted). */
    class UsblContentStorageFileSystem final : public ams::fs::fsa::IFileSystem, public ams::fs::impl::Newable {
        NON_COPYABLE(UsblContentStorageFileSystem);
        NON_MOVEABLE(UsblContentStorageFileSystem);
        private:
            std::unique_ptr<ams::fs::fsa::IFileSystem> m_base;
        public:
            explicit UsblContentStorageFileSystem(std::unique_ptr<ams::fs::fsa::IFileSystem> &&base) : m_base(std::move(base)) { /* ... */ }
        private:
            virtual Result DoGetEntryType(ams::fs::DirectoryEntryType *out, const ams::fs::Path &path) override;
            virtual Result DoOpenFile(std::unique_ptr<ams::fs::fsa::IFile> *out_file, const ams::fs::Path &path, ams::fs::OpenMode mode) override;
            virtual Result DoGetFileTimeStampRaw(ams::fs::FileTimeStampRaw *out, const ams::fs::Path &path) override;

            virtual Result DoCreateFile(const ams::fs::Path &path, s64 size, int flags) override;
            virtual Result DoDeleteFile(const ams::fs::Path &path) override;
            virtual Result DoCreateDirectory(const ams::fs::Path &path) override;
            virtual Result DoDeleteDirectory(const ams::fs::Path &path) override;
            virtual Result DoDeleteDirectoryRecursively(const ams::fs::Path &path) override;
            virtual Result DoRenameFile(const ams::fs::Path &old_path, const ams::fs::Path &new_path) override;
            virtual Result DoRenameDirectory(const ams::fs::Path &old_path, const ams::fs::Path &new_path) override;
            virtual Result DoOpenDirectory(std::unique_ptr<ams::fs::fsa::IDirectory> *out_dir, const ams::fs::Path &path, ams::fs::OpenDirectoryMode mode) override;
            virtual Result DoCommit() override;
            virtual Result DoGetFreeSpaceSize(s64 *out, const ams::fs::Path &path) override;
            virtual Result DoGetTotalSpaceSize(s64 *out, const ams::fs::Path &path) override;
            virtual Result DoCleanDirectoryRecursively(const ams::fs::Path &path) override;
            virtual Result DoQueryEntry(char *dst, size_t dst_size, const char *src, size_t src_size, ams::fs::fsa::QueryId query, const ams::fs::Path &path) override;
            virtual Result DoCommitProvisionally(s64 counter) override;
            virtual Result DoRollback() override;
            virtual Result DoFlush() override;
    };

}
