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
#include "usbl_ncz_storage.hpp"
#include "usbl_zstd.hpp"
#include "../storage/usbl_volume.hpp"
#include "../usbl_growable_array.hpp"
#include "../usbl_results.hpp"

namespace ams::mitm::usbl::nca {

    namespace {

        /* The NCZ layout (as the nsz tool writes it): the NCA header, then the sections, then the block table. */
        constexpr char SectionMagic[8] = { 'N', 'C', 'Z', 'S', 'E', 'C', 'T', 'N' };
        constexpr char BlockMagic[8]   = { 'N', 'C', 'Z', 'B', 'L', 'O', 'C', 'K' };
        constexpr u64 MaxSectionCount  = 0x40;

        struct RawSection {
            u64 offset;
            u64 size;
            u64 crypto_type;
            u64 padding;
            u8 key[0x10];
            u8 counter[0x10];
        };
        static_assert(sizeof(RawSection) == 0x40);

        struct BlockHeader {
            char magic[8];
            u8 version;
            u8 type;
            u8 unused;
            u8 block_size_exponent;
            u32 block_count;
            u64 decompressed_size;
        };
        static_assert(sizeof(BlockHeader) == 0x18);

        /* AES-CTR sections: 3 is plain CTR, 4 an update's (BKTR), whose ranges the nsz tool lists as their own sections. */
        constexpr u64 CryptoTypeCtr  = 3;
        constexpr u64 CryptoTypeBktr = 4;

        /* Blocks up to 4 MiB (nsz writes 1 MiB by default): two decompressed blocks and one compressed are kept. */
        constexpr u8 MinBlockSizeExponent = 14;
        constexpr u8 MaxBlockSizeExponent = 22;
        constexpr u32 CheckpointInterval = 256;
        constexpr s32 CachedBlockCount = 2;

        /* One cache for every NCZ open, under the volume lock: a game reads one at a time anyway. */
        struct CachedBlock {
            u64 owner;
            u32 block;
            u64 last_use;
        };

        constinit os::SdkMutex g_cache_lock;
        constinit std::unique_ptr<u8[]> g_block_buffers[CachedBlockCount] = {};
        constinit std::unique_ptr<u8[]> g_compressed_buffer = {};
        constinit size_t g_buffer_size = 0;
        constinit CachedBlock g_cached[CachedBlockCount] = {};
        constinit u64 g_use_counter = 0;
        constinit u64 g_next_id = 1;
        constinit s32 g_live_storages = 0;

        void ReleaseCache() {
            for (s32 i = 0; i < CachedBlockCount; ++i) {
                g_block_buffers[i].reset();
                g_cached[i] = {};
            }
            g_compressed_buffer.reset();
            g_buffer_size = 0;
            ReleaseZstd();
        }

        /* Grows the buffers for a block size; false when the heap has no room. */
        bool EnsureBuffers(size_t block_size) {
            if (block_size <= g_buffer_size) {
                return true;
            }
            ReleaseCache();
            if (!HasHeapRoomFor(block_size * (CachedBlockCount + 1))) {
                return false;
            }
            for (s32 i = 0; i < CachedBlockCount; ++i) {
                g_block_buffers[i].reset(new (std::nothrow) u8[block_size]);
            }
            g_compressed_buffer.reset(new (std::nothrow) u8[block_size]);
            if (std::any_of(std::begin(g_block_buffers), std::end(g_block_buffers), [](const auto &buffer) { return buffer == nullptr; }) || g_compressed_buffer == nullptr) {
                ReleaseCache();
                return false;
            }
            g_buffer_size = block_size;
            return true;
        }

        void StoreBe64(u8 *dst, u64 value) {
            for (s32 i = 0; i < 8; ++i) {
                dst[i] = static_cast<u8>(value >> (56 - 8 * i));
            }
        }

    }

    NczStorage::NczStorage() : m_base(), m_header(), m_sections(), m_section_count(0), m_block_sizes(), m_checkpoints(), m_block_count(0), m_block_size(0), m_data_offset(0), m_body_size(0), m_id(0), m_is_registered(false) { /* ... */ }

    NczStorage::~NczStorage() {
        if (m_is_registered) {
            std::scoped_lock lk(g_cache_lock);
            for (auto &cached : g_cached) {
                if (cached.owner == m_id) {
                    cached = {};
                }
            }
            /* The last NCZ closed: the memory goes back to the heap. */
            if (--g_live_storages == 0) {
                ReleaseCache();
            }
        }
    }

    Result NczStorage::Initialize(std::shared_ptr<ams::fs::IStorage> base) {
        s64 base_size = 0;
        R_TRY(base->GetSize(std::addressof(base_size)));
        R_UNLESS(base_size > HeaderSize + 0x10, usbl::ResultInvalidNczHeader());

        m_header.reset(new (std::nothrow) u8[HeaderSize]);
        R_UNLESS(m_header != nullptr, fs::ResultAllocationMemoryFailedNew());
        R_TRY(base->Read(0, m_header.get(), HeaderSize));

        struct { char magic[8]; u64 count; } section_header;
        R_TRY(base->Read(HeaderSize, std::addressof(section_header), sizeof(section_header)));
        R_UNLESS(std::memcmp(section_header.magic, SectionMagic, sizeof(SectionMagic)) == 0, usbl::ResultInvalidNczHeader());
        R_UNLESS(section_header.count > 0 && section_header.count <= MaxSectionCount, usbl::ResultInvalidNczHeader());

        m_section_count = static_cast<s32>(section_header.count);
        m_sections.reset(new (std::nothrow) Section[m_section_count]);
        R_UNLESS(m_sections != nullptr, fs::ResultAllocationMemoryFailedNew());
        s64 position = HeaderSize + sizeof(section_header);
        for (s32 i = 0; i < m_section_count; ++i, position += sizeof(RawSection)) {
            RawSection raw;
            R_TRY(base->Read(position, std::addressof(raw), sizeof(raw)));
            R_UNLESS(raw.offset % 0x10 == 0 && raw.offset <= static_cast<u64>(std::numeric_limits<s64>::max()) && raw.size <= static_cast<u64>(std::numeric_limits<s64>::max()), usbl::ResultInvalidNczHeader());
            m_sections[i] = { .offset = static_cast<s64>(raw.offset), .size = static_cast<s64>(raw.size), .crypto_type = raw.crypto_type, .key = {}, .counter = {} };
            std::memcpy(m_sections[i].key, raw.key, sizeof(raw.key));
            std::memcpy(m_sections[i].counter, raw.counter, sizeof(raw.counter));
        }

        BlockHeader block_header;
        R_TRY(base->Read(position, std::addressof(block_header), sizeof(block_header)));
        /* Without a block table the body is a single zstd stream (solid mode). */
        R_UNLESS(std::memcmp(block_header.magic, BlockMagic, sizeof(BlockMagic)) == 0, usbl::ResultUnsupportedNcz());
        R_UNLESS(MinBlockSizeExponent <= block_header.block_size_exponent && block_header.block_size_exponent <= MaxBlockSizeExponent, usbl::ResultUnsupportedNcz());
        position += sizeof(block_header);

        m_block_size  = 1u << block_header.block_size_exponent;
        m_body_size   = static_cast<s64>(block_header.decompressed_size);
        m_block_count = block_header.block_count;
        R_UNLESS(m_body_size > 0 && m_block_count == util::DivideUp<u64>(block_header.decompressed_size, m_block_size), usbl::ResultInvalidNczHeader());

        R_UNLESS(HasHeapRoomFor(m_block_count * sizeof(u32)), fs::ResultAllocationMemoryFailedNew());
        m_block_sizes.reset(new (std::nothrow) u32[m_block_count]);
        m_checkpoints.reset(new (std::nothrow) s64[util::DivideUp<u32>(m_block_count, CheckpointInterval)]);
        R_UNLESS(m_block_sizes != nullptr && m_checkpoints != nullptr, fs::ResultAllocationMemoryFailedNew());
        R_TRY(base->Read(position, m_block_sizes.get(), m_block_count * sizeof(u32)));
        m_data_offset = position + m_block_count * sizeof(u32);

        s64 offset = m_data_offset;
        for (u32 block = 0; block < m_block_count; ++block) {
            if (block % CheckpointInterval == 0) {
                m_checkpoints[block / CheckpointInterval] = offset;
            }
            R_UNLESS(0 < m_block_sizes[block] && m_block_sizes[block] <= this->GetBlockLength(block), usbl::ResultInvalidNczHeader());
            offset += m_block_sizes[block];
        }
        R_UNLESS(offset <= base_size, usbl::ResultInvalidNczHeader());

        m_base = std::move(base);
        {
            std::scoped_lock lk(g_cache_lock);
            m_id = g_next_id++;
            ++g_live_storages;
            m_is_registered = true;
        }
        R_SUCCEED();
    }

    s64 NczStorage::GetBlockOffset(u32 block) const {
        const u32 checkpoint = block / CheckpointInterval;
        s64 offset = m_checkpoints[checkpoint];
        for (u32 i = checkpoint * CheckpointInterval; i < block; ++i) {
            offset += m_block_sizes[i];
        }
        return offset;
    }

    u32 NczStorage::GetBlockLength(u32 block) const {
        const s64 start = static_cast<s64>(block) * m_block_size;
        return static_cast<u32>(std::min<s64>(m_block_size, m_body_size - start));
    }

    Result NczStorage::LoadBlock(u8 *dst, u8 *compressed, u32 block) {
        const u32 length = this->GetBlockLength(block);
        const u32 stored = m_block_sizes[block];
        const s64 offset = this->GetBlockOffset(block);

        /* A block that would not shrink is stored as it is. */
        if (stored == length) {
            R_RETURN(m_base->Read(offset, dst, length));
        }
        R_TRY(m_base->Read(offset, compressed, stored));
        R_RETURN(DecompressZstdFrame(dst, length, compressed, stored));
    }

    void NczStorage::EncryptBlock(u8 *data, u32 block) const {
        const s64 start = HeaderSize + static_cast<s64>(block) * m_block_size;
        const s64 end   = start + this->GetBlockLength(block);
        for (s32 i = 0; i < m_section_count; ++i) {
            const Section &section = m_sections[i];
            if (section.crypto_type != CryptoTypeCtr && section.crypto_type != CryptoTypeBktr) {
                continue;
            }
            const s64 from = std::max(start, section.offset);
            const s64 to   = std::min(end, section.offset + section.size);
            if (from >= to) {
                continue;
            }

            /* The counter's low half is the offset in the NCA in 16-byte units; blocks and sections both start aligned. */
            u8 iv[0x10];
            std::memcpy(iv, section.counter, 8);
            StoreBe64(iv + 8, static_cast<u64>(from) >> 4);
            u8 *range = data + (from - start);
            crypto::EncryptAes128Ctr(range, to - from, section.key, sizeof(section.key), iv, sizeof(iv), range, to - from);
        }
    }

    Result NczStorage::GetBlock(const u8 **out, u32 block) {
        AMS_ASSERT(g_cache_lock.IsLockedByCurrentThread());

        s32 slot = 0;
        for (s32 i = 0; i < CachedBlockCount; ++i) {
            if (g_cached[i].owner == m_id && g_cached[i].block == block) {
                g_cached[i].last_use = ++g_use_counter;
                *out = g_block_buffers[i].get();
                R_SUCCEED();
            }
            if (g_cached[i].last_use < g_cached[slot].last_use) {
                slot = i;
            }
        }

        R_UNLESS(EnsureBuffers(m_block_size), fs::ResultAllocationMemoryFailedNew());
        g_cached[slot] = {};
        u8 *data = g_block_buffers[slot].get();
        R_TRY(this->LoadBlock(data, g_compressed_buffer.get(), block));
        this->EncryptBlock(data, block);

        g_cached[slot] = { .owner = m_id, .block = block, .last_use = ++g_use_counter };
        *out = data;
        R_SUCCEED();
    }

    Result NczStorage::Read(s64 offset, void *buffer, size_t size) {
        R_SUCCEED_IF(size == 0);
        R_UNLESS(buffer != nullptr, fs::ResultNullptrArgument());
        R_TRY(IStorage::CheckAccessRange(offset, size, this->GetNcaSize()));

        u8 *dst = static_cast<u8 *>(buffer);
        if (offset < HeaderSize) {
            const size_t count = std::min<size_t>(size, HeaderSize - offset);
            std::memcpy(dst, m_header.get() + offset, count);
            dst += count;
            offset += count;
            size -= count;
        }

        /* The volume lock first, as every read of the drive takes it: the indexer holds it while it opens an NCZ. */
        std::scoped_lock volume_lk(GetVolumeLock());
        std::scoped_lock lk(g_cache_lock);
        while (size > 0) {
            const s64 body_offset = offset - HeaderSize;
            const u32 block = static_cast<u32>(body_offset / m_block_size);
            const u32 in_block = static_cast<u32>(body_offset % m_block_size);

            const u8 *data;
            R_TRY(this->GetBlock(std::addressof(data), block));
            const size_t count = std::min<size_t>(size, this->GetBlockLength(block) - in_block);
            std::memcpy(dst, data + in_block, count);
            dst += count;
            offset += count;
            size -= count;
        }
        R_SUCCEED();
    }

    Result NczStorage::GetSize(s64 *out) {
        *out = this->GetNcaSize();
        R_SUCCEED();
    }

    Result NczStorage::Write(s64 offset, const void *buffer, size_t size) {
        AMS_UNUSED(offset, buffer, size);
        R_THROW(fs::ResultUnsupportedOperation());
    }

    Result NczStorage::Flush() {
        R_SUCCEED();
    }

    Result NczStorage::SetSize(s64 size) {
        AMS_UNUSED(size);
        R_THROW(fs::ResultUnsupportedOperation());
    }

    Result NczStorage::OperateRange(void *dst, size_t dst_size, ams::fs::OperationId op_id, s64 offset, s64 size, const void *src, size_t src_size) {
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
