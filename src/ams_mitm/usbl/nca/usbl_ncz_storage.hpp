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

namespace ams::mitm::usbl::nca {

    /* An NCZ (each NCA of an NSZ or XCZ) read back as the NCA it was made from: the NCA header kept as it was, the rest
     * decompressed block by block and encrypted again with each section's key. Only block mode: a solid NCZ is one zstd
     * stream with no way to start reading in the middle. */
    class NczStorage : public ams::fs::IStorage, public ams::fs::impl::Newable {
        NON_COPYABLE(NczStorage);
        NON_MOVEABLE(NczStorage);
        public:
            static constexpr s64 HeaderSize = 0x4000;

            struct Section {
                s64 offset;
                s64 size;
                u64 crypto_type;
                u8 key[0x10];
                u8 counter[0x10];
            };
        private:
            std::shared_ptr<ams::fs::IStorage> m_base;
            std::unique_ptr<u8[]> m_header;
            std::unique_ptr<Section[]> m_sections;
            s32 m_section_count;
            std::unique_ptr<u32[]> m_block_sizes;
            /* Where every 256th block starts in the NCZ; the ones in between add up the sizes from there. */
            std::unique_ptr<s64[]> m_checkpoints;
            u32 m_block_count;
            u32 m_block_size;
            s64 m_data_offset;
            s64 m_body_size;
            u64 m_id;
            bool m_is_registered;
        public:
            NczStorage();
            virtual ~NczStorage();

            /* base: the NCZ bytes. UnsupportedNcz for a solid NCZ or blocks larger than the cache takes. */
            Result Initialize(std::shared_ptr<ams::fs::IStorage> base);

            /* Size of the NCA it stands for. */
            s64 GetNcaSize() const { return HeaderSize + m_body_size; }

            virtual Result Read(s64 offset, void *buffer, size_t size) override;
            virtual Result GetSize(s64 *out) override;

            virtual Result Write(s64 offset, const void *buffer, size_t size) override;
            virtual Result Flush() override;
            virtual Result SetSize(s64 size) override;
            virtual Result OperateRange(void *dst, size_t dst_size, ams::fs::OperationId op_id, s64 offset, s64 size, const void *src, size_t src_size) override;
        private:
            s64 GetBlockOffset(u32 block) const;
            u32 GetBlockLength(u32 block) const;
            Result LoadBlock(u8 *dst, u8 *compressed, u32 block);
            void EncryptBlock(u8 *data, u32 block) const;
            Result GetBlock(const u8 **out, u32 block);
    };

}
