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
#include "fsmitm_usbl_content_storage_fs.hpp"
#include "fsmitm_usbl_content.hpp"

namespace ams::mitm::fs {

    namespace {

        class UsblNcaFile final : public ams::fs::fsa::IFile, public ams::fs::impl::Newable {
            NON_COPYABLE(UsblNcaFile);
            NON_MOVEABLE(UsblNcaFile);
            private:
                std::shared_ptr<ams::fs::IStorage> m_storage;
                s64 m_size;
            public:
                UsblNcaFile(std::shared_ptr<ams::fs::IStorage> &&storage, s64 size) : m_storage(std::move(storage)), m_size(size) { /* ... */ }

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
                    R_THROW(ams::fs::ResultUnsupportedOperation());
                }

                virtual Result DoSetSize(s64 size) override {
                    AMS_UNUSED(size);
                    R_THROW(ams::fs::ResultUnsupportedOperation());
                }

                virtual Result DoOperateRange(void *dst, size_t dst_size, ams::fs::OperationId op_id, s64 offset, s64 size, const void *src, size_t src_size) override {
                    R_RETURN(m_storage->OperateRange(dst, dst_size, op_id, offset, size, src, src_size));
                }
        };

        bool FindServedContent(ncm::ContentId *out, const ams::fs::Path &path) {
            return IsUsblContentPath(out, path.GetString());
        }

    }

    Result UsblContentStorageFileSystem::DoGetEntryType(ams::fs::DirectoryEntryType *out, const ams::fs::Path &path) {
        if (ncm::ContentId content_id; FindServedContent(std::addressof(content_id), path)) {
            *out = ams::fs::DirectoryEntryType_File;
            R_SUCCEED();
        }

        R_RETURN(m_base->GetEntryType(out, path));
    }

    Result UsblContentStorageFileSystem::DoOpenFile(std::unique_ptr<ams::fs::fsa::IFile> *out_file, const ams::fs::Path &path, ams::fs::OpenMode mode) {
        ncm::ContentId content_id;
        if (!FindServedContent(std::addressof(content_id), path)) {
            R_RETURN(m_base->OpenFile(out_file, path, mode));
        }

        R_UNLESS((mode & ams::fs::OpenMode_Write) == 0, ams::fs::ResultUnsupportedOperation());

        std::shared_ptr<ams::fs::IStorage> raw_nca;
        R_TRY(OpenUsblRawContentStorage(std::addressof(raw_nca), content_id));

        s64 size;
        R_TRY(raw_nca->GetSize(std::addressof(size)));

        auto file = std::make_unique<UsblNcaFile>(std::move(raw_nca), size);
        R_UNLESS(file != nullptr, ams::fs::ResultAllocationMemoryFailedNew());

        *out_file = std::move(file);
        R_SUCCEED();
    }

    Result UsblContentStorageFileSystem::DoGetFileTimeStampRaw(ams::fs::FileTimeStampRaw *out, const ams::fs::Path &path) {
        if (ncm::ContentId content_id; FindServedContent(std::addressof(content_id), path)) {
            *out = {};
            R_SUCCEED();
        }

        R_RETURN(m_base->GetFileTimeStampRaw(out, path));
    }

    Result UsblContentStorageFileSystem::DoCreateFile(const ams::fs::Path &path, s64 size, int flags)          { R_RETURN(m_base->CreateFile(path, size, flags)); }
    Result UsblContentStorageFileSystem::DoDeleteFile(const ams::fs::Path &path)                                { R_RETURN(m_base->DeleteFile(path)); }
    Result UsblContentStorageFileSystem::DoCreateDirectory(const ams::fs::Path &path)                           { R_RETURN(m_base->CreateDirectory(path)); }
    Result UsblContentStorageFileSystem::DoDeleteDirectory(const ams::fs::Path &path)                           { R_RETURN(m_base->DeleteDirectory(path)); }
    Result UsblContentStorageFileSystem::DoDeleteDirectoryRecursively(const ams::fs::Path &path)                { R_RETURN(m_base->DeleteDirectoryRecursively(path)); }
    Result UsblContentStorageFileSystem::DoRenameFile(const ams::fs::Path &old_path, const ams::fs::Path &new_path)      { R_RETURN(m_base->RenameFile(old_path, new_path)); }
    Result UsblContentStorageFileSystem::DoRenameDirectory(const ams::fs::Path &old_path, const ams::fs::Path &new_path) { R_RETURN(m_base->RenameDirectory(old_path, new_path)); }
    Result UsblContentStorageFileSystem::DoOpenDirectory(std::unique_ptr<ams::fs::fsa::IDirectory> *out_dir, const ams::fs::Path &path, ams::fs::OpenDirectoryMode mode) { R_RETURN(m_base->OpenDirectory(out_dir, path, mode)); }
    Result UsblContentStorageFileSystem::DoCommit()                                                             { R_RETURN(m_base->Commit()); }
    Result UsblContentStorageFileSystem::DoGetFreeSpaceSize(s64 *out, const ams::fs::Path &path)                { R_RETURN(m_base->GetFreeSpaceSize(out, path)); }
    Result UsblContentStorageFileSystem::DoGetTotalSpaceSize(s64 *out, const ams::fs::Path &path)               { R_RETURN(m_base->GetTotalSpaceSize(out, path)); }
    Result UsblContentStorageFileSystem::DoCleanDirectoryRecursively(const ams::fs::Path &path)                 { R_RETURN(m_base->CleanDirectoryRecursively(path)); }
    Result UsblContentStorageFileSystem::DoQueryEntry(char *dst, size_t dst_size, const char *src, size_t src_size, ams::fs::fsa::QueryId query, const ams::fs::Path &path) { R_RETURN(m_base->QueryEntry(dst, dst_size, src, src_size, query, path)); }
    Result UsblContentStorageFileSystem::DoCommitProvisionally(s64 counter)                                     { R_RETURN(m_base->CommitProvisionally(counter)); }
    Result UsblContentStorageFileSystem::DoRollback()                                                           { R_RETURN(m_base->Rollback()); }
    Result UsblContentStorageFileSystem::DoFlush()                                                              { R_RETURN(m_base->Flush()); }

}
