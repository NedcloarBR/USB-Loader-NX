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

namespace ams::mitm::usbl::usb {

    struct InquiryData {
        char vendor[8 + 1];
        char product[16 + 1];
        char revision[4 + 1];
    };

    class MassStorageDevice {
        NON_COPYABLE(MassStorageDevice);
        NON_MOVEABLE(MassStorageDevice);
        public:
            /* Up to 256 KB per command; a drive that fails one falls back to 64 KB, a size every drive takes. */
            static constexpr size_t TransferBufferSize = 256_KB;
            static constexpr size_t SafeTransferSize   = 64_KB;
        private:
            enum class Direction : u8 {
                Out = 0x00,
                In  = 0x80,
            };

            struct Command {
                u8 cdb[16];
                u8 cdb_length;
                Direction direction;
                u32 data_length;
            };
        private:
            ::UsbHsClientIfSession m_interface_session;
            ::UsbHsClientEpSession m_in_endpoint;
            ::UsbHsClientEpSession m_out_endpoint;
            u32 m_tag;
            u8 m_lun;
            u32 m_block_size;
            u64 m_block_count;
            bool m_is_open;
            u32 m_max_transfer_size;

            /* Per device: several drives run commands concurrently. usb:hs wants page-aligned buffers. */
            alignas(os::MemoryPageSize) u8 m_transfer_buffer[TransferBufferSize];
            alignas(os::MemoryPageSize) u8 m_command_buffer[os::MemoryPageSize];
            alignas(os::MemoryPageSize) u8 m_status_buffer[os::MemoryPageSize];
            alignas(os::MemoryPageSize) u8 m_control_buffer[os::MemoryPageSize];
        public:
            constexpr MassStorageDevice() : m_interface_session(), m_in_endpoint(), m_out_endpoint(), m_tag(0), m_lun(0), m_block_size(0), m_block_count(0), m_is_open(false), m_max_transfer_size(TransferBufferSize), m_transfer_buffer(), m_command_buffer(), m_status_buffer(), m_control_buffer() { /* ... */ }
            ~MassStorageDevice() { this->Close(); }

            Result Open(::UsbHsInterface *intf);
            void Close();

            Result Inquiry(InquiryData *out);
            Result Read(void *dst, u64 lba, u32 block_count);

            s32 GetInterfaceId() const { return m_interface_session.ID; }
            u32 GetBlockSize() const { return m_block_size; }
            u64 GetBlockCount() const { return m_block_count; }
        private:
            Result OpenEndpoints();
            Result WaitUntilReady();
            Result ReadCapacity();

            Result Execute(const Command &cmd, void *data, u32 *out_transferred = nullptr);
            Result ExecuteOnce(const Command &cmd, void *data, u32 *out_transferred);
            Result ReceiveStatus(u32 tag);
            Result RequestSense();

            Result ResetRecovery();
            Result ClearHalt(const ::UsbHsClientEpSession &ep);
    };

}
