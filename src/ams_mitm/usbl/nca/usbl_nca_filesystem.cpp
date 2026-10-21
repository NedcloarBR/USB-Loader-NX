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
#include "usbl_nca_filesystem.hpp"
#include "usbl_nca_crypto.hpp"
#include "../usbl_results.hpp"

namespace ams::mitm::usbl::nca {

    namespace {

        /* Hash layers take their tables and a two-block cache from the buffer pool; loader mounts the ExeFS three times at once. */
        /* fssystem retries buffer manager allocations forever, so an undersized buffer manager hangs the reader instead of failing. */
        /* RomFS is served by fs_mitm through ams.mitm's own fssystem creators. */
        constexpr size_t BufferPoolSize        = 2_MB;
        constexpr size_t BufferManagerHeapSize = 2_MB;
        constexpr size_t MaxCacheCount         = 256;
        constexpr size_t BlockSize             = 16_KB;

        alignas(os::MemoryPageSize) constinit u8 g_buffer_pool[BufferPoolSize] = {};
        alignas(os::MemoryPageSize) constinit u8 g_buffer_manager_heap[BufferManagerHeapSize] = {};

        constinit util::TypedStorage<mem::StandardAllocator> g_buffer_allocator = {};
        constinit util::TypedStorage<fssrv::MemoryResourceFromStandardAllocator> g_allocator = {};
        constinit util::TypedStorage<fssystem::FileSystemBufferManager> g_buffer_manager = {};

        constinit util::TypedStorage<fssrv::fscreator::RomFileSystemCreator>       g_rom_fs_creator = {};
        constinit util::TypedStorage<fssrv::fscreator::PartitionFileSystemCreator> g_partition_fs_creator = {};
        constinit util::TypedStorage<fssrv::fscreator::StorageOnNcaCreator>        g_storage_on_nca_creator = {};

        /* The key derivation failure flag is process-wide; reader creation (header only) is serialized around it. */
        constinit os::SdkMutex g_open_reader_lock;

    }

    void InitializeFileSystem() {
        util::ConstructAt(g_buffer_allocator, g_buffer_pool, BufferPoolSize);
        util::ConstructAt(g_allocator, GetPointer(g_buffer_allocator));

        /* The fssystem allocators and the pooled buffer heap are process-wide and already set up by */
        /* fssystem::InitializeForAtmosphereMitm; re-initializing them would corrupt ams.mitm's own reads. */
        util::ConstructAt(g_buffer_manager);
        R_ABORT_UNLESS(GetReference(g_buffer_manager).Initialize(MaxCacheCount, reinterpret_cast<uintptr_t>(g_buffer_manager_heap), BufferManagerHeapSize, BlockSize));

        util::ConstructAt(g_rom_fs_creator, GetPointer(g_allocator));
        util::ConstructAt(g_partition_fs_creator);
        util::ConstructAt(g_storage_on_nca_creator, GetPointer(g_allocator), GetCryptoConfiguration(), *fssystem::GetNcaCompressionConfiguration(), GetPointer(g_buffer_manager), fs::impl::GetNcaHashGeneratorFactorySelector());
    }

    Result OpenReader(std::shared_ptr<fssystem::NcaReader> *out, std::shared_ptr<fs::IStorage> raw_nca) {
        bool is_resigned;
        R_RETURN(OpenReader(out, std::addressof(is_resigned), std::move(raw_nca)));
    }

    Result OpenReader(std::shared_ptr<fssystem::NcaReader> *out, bool *out_is_resigned, std::shared_ptr<fs::IStorage> raw_nca) {
        std::scoped_lock lk(g_open_reader_lock);

        ConsumeKeyGenerationFailure();
        ConsumeResignedHeaderAcceptance();
        R_TRY(GetReference(g_storage_on_nca_creator).CreateNcaReader(out, std::move(raw_nca)));
        R_UNLESS(!ConsumeKeyGenerationFailure(), usbl::ResultKeyGenerationUnavailable());
        *out_is_resigned = ConsumeResignedHeaderAcceptance();
        R_SUCCEED();
    }

    Result OpenSectionFileSystem(std::shared_ptr<fs::fsa::IFileSystem> *out, std::shared_ptr<fssystem::NcaReader> reader, s32 section_index) {
        std::shared_ptr<fs::IStorage> storage;
        std::shared_ptr<fssystem::IAsynchronousAccessSplitter> splitter;
        fssystem::NcaFsHeaderReader header_reader;
        R_TRY(GetReference(g_storage_on_nca_creator).Create(std::addressof(storage), std::addressof(splitter), std::addressof(header_reader), std::move(reader), section_index));

        switch (header_reader.GetFsType()) {
            case fssystem::NcaFsHeader::FsType::PartitionFs: R_RETURN(GetReference(g_partition_fs_creator).Create(out, std::move(storage)));
            case fssystem::NcaFsHeader::FsType::RomFs:       R_RETURN(GetReference(g_rom_fs_creator).Create(out, std::move(storage)));
            default:                                         R_THROW(fs::ResultInvalidNcaFileSystemType());
        }
    }

}
