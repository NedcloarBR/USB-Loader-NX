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
#include "fsmitm_usbl_badge_fs.hpp"
#include "../usbl/usbl_log.hpp"
#include "../usbl/badge/usbl_badge.hpp"

namespace ams::mitm::fs {

    namespace {

        /* NACP icons are 256x256 JPEGs (about 30-100 KB); anything far larger is not an icon. */
        constexpr s64 MaxIconFileSize = 1_MB;

        class BadgedIconFile final : public ams::fs::fsa::IFile, public ams::fs::impl::Newable {
            NON_COPYABLE(BadgedIconFile);
            NON_MOVEABLE(BadgedIconFile);
            private:
                std::unique_ptr<u8[]> m_data;
                size_t m_size;
            public:
                BadgedIconFile(std::unique_ptr<u8[]> &&data, size_t size) : m_data(std::move(data)), m_size(size) { /* ... */ }

                virtual sf::cmif::DomainObjectId GetDomainObjectId() const override {
                    return sf::cmif::InvalidDomainObjectId;
                }
            private:
                virtual Result DoRead(size_t *out, s64 offset, void *buffer, size_t size, const ams::fs::ReadOption &option) override {
                    AMS_UNUSED(option);
                    R_UNLESS(offset >= 0, ams::fs::ResultOutOfRange());

                    const size_t read_size = static_cast<size_t>(offset) < m_size ? std::min(size, m_size - static_cast<size_t>(offset)) : 0;
                    std::memcpy(buffer, m_data.get() + offset, read_size);
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
                    AMS_UNUSED(offset, size, src, src_size);

                    switch (op_id) {
                        case ams::fs::OperationId::Invalidate:
                            R_SUCCEED();
                        case ams::fs::OperationId::QueryRange:
                            R_UNLESS(dst != nullptr,                              ams::fs::ResultNullptrArgument());
                            R_UNLESS(dst_size == sizeof(ams::fs::QueryRangeInfo), ams::fs::ResultInvalidSize());
                            reinterpret_cast<ams::fs::QueryRangeInfo *>(dst)->Clear();
                            R_SUCCEED();
                        default:
                            R_THROW(ams::fs::ResultUnsupportedOperation());
                    }
                }
        };

        bool IsIconPath(const char *path) {
            constexpr const char Prefix[] = "/icon_";
            constexpr const char Suffix[] = ".dat";

            const size_t len = std::strlen(path);
            return len > sizeof(Prefix) - 1 + sizeof(Suffix) - 1 && std::strncmp(path, Prefix, sizeof(Prefix) - 1) == 0 && std::strcmp(path + len - (sizeof(Suffix) - 1), Suffix) == 0;
        }

        class BadgedControlFileSystem final : public ams::fs::fsa::IFileSystem, public ams::fs::impl::Newable {
            NON_COPYABLE(BadgedControlFileSystem);
            NON_MOVEABLE(BadgedControlFileSystem);
            private:
                std::shared_ptr<ams::fs::fsa::IFileSystem> m_base;
                usbl::BadgeSource m_source;
            public:
                BadgedControlFileSystem(std::shared_ptr<ams::fs::fsa::IFileSystem> &&base, usbl::BadgeSource source) : m_base(std::move(base)), m_source(source) { /* ... */ }
            private:
                Result OpenBadgedIcon(std::unique_ptr<ams::fs::fsa::IFile> *out_file, const ams::fs::Path &path) {
                    std::unique_ptr<ams::fs::fsa::IFile> icon_file;
                    R_TRY(m_base->OpenFile(std::addressof(icon_file), path, ams::fs::OpenMode_Read));

                    s64 size = 0;
                    R_TRY(icon_file->GetSize(std::addressof(size)));
                    R_UNLESS(0 < size && size <= MaxIconFileSize, ams::fs::ResultInvalidSize());

                    auto jpeg = std::unique_ptr<u8[]>(new (std::nothrow) u8[size]);
                    R_UNLESS(jpeg != nullptr, ams::fs::ResultAllocationMemoryFailedNew());

                    size_t read = 0;
                    R_TRY(icon_file->Read(std::addressof(read), 0, jpeg.get(), size, ams::fs::ReadOption::None));
                    R_UNLESS(read == static_cast<size_t>(size), ams::fs::ResultOutOfRange());

                    std::unique_ptr<u8[]> badged;
                    size_t badged_size = 0;
                    R_TRY(usbl::badge::ApplyToJpeg(std::addressof(badged), std::addressof(badged_size), jpeg.get(), read, m_source));

                    auto file = std::make_unique<BadgedIconFile>(std::move(badged), badged_size);
                    R_UNLESS(file != nullptr, ams::fs::ResultAllocationMemoryFailedNew());

                    *out_file = std::move(file);
                    R_SUCCEED();
                }

                virtual Result DoOpenFile(std::unique_ptr<ams::fs::fsa::IFile> *out_file, const ams::fs::Path &path, ams::fs::OpenMode mode) override {
                    if (mode == ams::fs::OpenMode_Read && IsIconPath(path.GetString())) {
                        /* Games only ship some languages' icons; a missing one is not worth a log line. */
                        const Result result = this->OpenBadgedIcon(out_file, path);
                        R_SUCCEED_IF(R_SUCCEEDED(result));
                        if (!ams::fs::ResultPathNotFound::Includes(result)) {
                            usbl::Log("fs.mitm: badge not applied to %s: 2%03d-%04d", path.GetString(), result.GetModule(), result.GetDescription());
                        }
                    }
                    R_RETURN(m_base->OpenFile(out_file, path, mode));
                }

                virtual Result DoGetEntryType(ams::fs::DirectoryEntryType *out, const ams::fs::Path &path) override                                          { R_RETURN(m_base->GetEntryType(out, path)); }
                virtual Result DoOpenDirectory(std::unique_ptr<ams::fs::fsa::IDirectory> *out_dir, const ams::fs::Path &path, ams::fs::OpenDirectoryMode mode) override { R_RETURN(m_base->OpenDirectory(out_dir, path, mode)); }
                virtual Result DoCreateFile(const ams::fs::Path &path, s64 size, int flags) override                                                       { R_RETURN(m_base->CreateFile(path, size, flags)); }
                virtual Result DoDeleteFile(const ams::fs::Path &path) override                                                                             { R_RETURN(m_base->DeleteFile(path)); }
                virtual Result DoCreateDirectory(const ams::fs::Path &path) override                                                                        { R_RETURN(m_base->CreateDirectory(path)); }
                virtual Result DoDeleteDirectory(const ams::fs::Path &path) override                                                                        { R_RETURN(m_base->DeleteDirectory(path)); }
                virtual Result DoDeleteDirectoryRecursively(const ams::fs::Path &path) override                                                             { R_RETURN(m_base->DeleteDirectoryRecursively(path)); }
                virtual Result DoRenameFile(const ams::fs::Path &old_path, const ams::fs::Path &new_path) override                                          { R_RETURN(m_base->RenameFile(old_path, new_path)); }
                virtual Result DoRenameDirectory(const ams::fs::Path &old_path, const ams::fs::Path &new_path) override                                     { R_RETURN(m_base->RenameDirectory(old_path, new_path)); }
                virtual Result DoCommit() override                                                                                                          { R_RETURN(m_base->Commit()); }
                virtual Result DoCleanDirectoryRecursively(const ams::fs::Path &path) override                                                              { R_RETURN(m_base->CleanDirectoryRecursively(path)); }
                virtual Result DoGetFileTimeStampRaw(ams::fs::FileTimeStampRaw *out, const ams::fs::Path &path) override                                    { R_RETURN(m_base->GetFileTimeStampRaw(out, path)); }
                virtual Result DoQueryEntry(char *dst, size_t dst_size, const char *src, size_t src_size, ams::fs::fsa::QueryId query, const ams::fs::Path &path) override { R_RETURN(m_base->QueryEntry(dst, dst_size, src, src_size, query, path)); }
        };

    }

    std::shared_ptr<ams::fs::fsa::IFileSystem> MakeUsblBadgedControlFileSystem(std::shared_ptr<ams::fs::fsa::IFileSystem> &&base, usbl::BadgeSource source) {
        if (!usbl::badge::IsEnabled()) {
            return std::move(base);
        }

        return std::make_shared<BadgedControlFileSystem>(std::move(base), source);
    }

}
