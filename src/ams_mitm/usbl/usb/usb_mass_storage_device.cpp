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
#include "usb_mass_storage_device.hpp"
#include "../usbl_results.hpp"
#include "../usbl_log.hpp"

namespace ams::mitm::usbl::usb {

    namespace {

        constexpr u32 CommandBlockWrapperSignature = 0x43425355;
        constexpr u32 CommandStatusWrapperSignature = 0x53425355;

        constexpr size_t CommandBlockWrapperSize  = 31;
        constexpr size_t CommandStatusWrapperSize = 13;

        constexpr u8 CommandStatusPassed     = 0x00;
        constexpr u8 CommandStatusFailed     = 0x01;
        constexpr u8 CommandStatusPhaseError = 0x02;

        constexpr u8 RequestTypeClassInterfaceOut   = 0x21;
        constexpr u8 RequestTypeStandardEndpointOut = 0x02;
        constexpr u8 RequestBulkOnlyReset           = 0xFF;
        constexpr u8 RequestClearFeature            = 0x01;
        constexpr u16 FeatureEndpointHalt           = 0x00;

        constexpr u8 EndpointDirectionIn      = 0x80;
        constexpr u8 EndpointTransferTypeMask = 0x03;
        constexpr u8 EndpointTransferTypeBulk = 0x02;
        constexpr size_t EndpointDescriptorCount = 15;

        constexpr u8 ScsiTestUnitReady  = 0x00;
        constexpr u8 ScsiRequestSense   = 0x03;
        constexpr u8 ScsiInquiry        = 0x12;
        constexpr u8 ScsiReadCapacity10 = 0x25;
        constexpr u8 ScsiRead10         = 0x28;
        constexpr u8 ScsiRead16         = 0x88;
        constexpr u8 ScsiReadCapacity16 = 0x9E;
        constexpr u8 ServiceActionReadCapacity16 = 0x10;

        constexpr u32 InquiryDataSize        = 36;
        constexpr u32 RequestSenseDataSize   = 18;
        constexpr u32 ReadCapacity10DataSize = 8;
        constexpr u32 ReadCapacity16DataSize = 32;

        constexpr int UnitReadyRetryCount = 20;
        constexpr TimeSpan UnitReadyRetryInterval = TimeSpan::FromMilliSeconds(250);

        void StoreLe32(u8 *dst, u32 v) {
            for (size_t i = 0; i < sizeof(v); ++i) {
                dst[i] = static_cast<u8>(v >> (8 * i));
            }
        }

        u32 LoadLe32(const u8 *src) {
            u32 v = 0;
            for (size_t i = 0; i < sizeof(v); ++i) {
                v |= static_cast<u32>(src[i]) << (8 * i);
            }
            return v;
        }

        template<typename T>
        void StoreBe(u8 *dst, T v) {
            for (size_t i = 0; i < sizeof(T); ++i) {
                dst[i] = static_cast<u8>(v >> (8 * (sizeof(T) - 1 - i)));
            }
        }

        template<typename T>
        T LoadBe(const u8 *src) {
            T v = 0;
            for (size_t i = 0; i < sizeof(T); ++i) {
                v = static_cast<T>((v << 8) | src[i]);
            }
            return v;
        }

        void CopyTrimmedAscii(char *dst, const u8 *src, size_t len) {
            std::memcpy(dst, src, len);
            dst[len] = '\x00';
            for (size_t i = len; i > 0 && (dst[i - 1] == ' ' || dst[i - 1] == '\x00'); --i) {
                dst[i - 1] = '\x00';
            }
        }

        bool IsBulkEndpoint(const usb_endpoint_descriptor &desc) {
            return desc.bLength != 0 && (desc.bmAttributes & EndpointTransferTypeMask) == EndpointTransferTypeBulk;
        }

        bool IsSupportedBlockSize(u32 block_size) {
            return block_size == 512 || block_size == 1024 || block_size == 2048 || block_size == 4096;
        }

        Result PostBuffer(::UsbHsClientEpSession *ep, void *buffer, u32 size, u32 *out_transferred) {
            R_RETURN(::usbHsEpPostBuffer(ep, buffer, size, out_transferred));
        }

    }

    Result MassStorageDevice::Open(::UsbHsInterface *intf) {
        AMS_ABORT_UNLESS(!m_is_open);

        R_TRY(::usbHsAcquireUsbIf(std::addressof(m_interface_session), intf));
        m_is_open = true;
        m_max_transfer_size = TransferBufferSize;
        auto close_guard = SCOPE_GUARD { this->Close(); };

        R_TRY(this->OpenEndpoints());
        R_TRY(this->WaitUntilReady());
        R_TRY(this->ReadCapacity());

        close_guard.Cancel();
        R_SUCCEED();
    }

    void MassStorageDevice::Close() {
        if (!m_is_open) {
            return;
        }

        ::usbHsEpClose(std::addressof(m_in_endpoint));
        ::usbHsEpClose(std::addressof(m_out_endpoint));
        ::usbHsIfClose(std::addressof(m_interface_session));

        m_block_size  = 0;
        m_block_count = 0;
        m_is_open     = false;
    }

    Result MassStorageDevice::OpenEndpoints() {
        const auto &info = m_interface_session.inf.inf;

        const usb_endpoint_descriptor *in_desc  = nullptr;
        const usb_endpoint_descriptor *out_desc = nullptr;

        auto consider = [&](const usb_endpoint_descriptor &desc) {
            if (!IsBulkEndpoint(desc)) {
                return;
            }
            const bool is_in = (desc.bEndpointAddress & EndpointDirectionIn) != 0;
            auto &slot = is_in ? in_desc : out_desc;
            if (slot == nullptr) {
                slot = std::addressof(desc);
            }
        };

        for (size_t i = 0; i < EndpointDescriptorCount; ++i) {
            consider(info.input_endpoint_descs[i]);
            consider(info.output_endpoint_descs[i]);
        }

        R_UNLESS(in_desc != nullptr && out_desc != nullptr, usbl::ResultNoBulkEndpoints());

        auto in_copy  = *in_desc;
        auto out_copy = *out_desc;
        R_TRY(::usbHsIfOpenUsbEp(std::addressof(m_interface_session), std::addressof(m_in_endpoint), 1, TransferBufferSize, std::addressof(in_copy)));
        R_TRY(::usbHsIfOpenUsbEp(std::addressof(m_interface_session), std::addressof(m_out_endpoint), 1, TransferBufferSize, std::addressof(out_copy)));

        R_SUCCEED();
    }

    Result MassStorageDevice::WaitUntilReady() {
        const Command cmd = { .cdb = { ScsiTestUnitReady }, .cdb_length = 6, .direction = Direction::Out, .data_length = 0 };

        /* Drives commonly report UNIT ATTENTION / NOT READY right after enumeration; sensing clears it. */
        for (int i = 0; i < UnitReadyRetryCount; ++i) {
            if (R_SUCCEEDED(this->Execute(cmd, nullptr))) {
                R_SUCCEED();
            }
            os::SleepThread(UnitReadyRetryInterval);
        }

        R_THROW(usbl::ResultUnitNotReady());
    }

    Result MassStorageDevice::ReadCapacity() {
        u8 data[ReadCapacity16DataSize] = {};

        const Command cmd10 = { .cdb = { ScsiReadCapacity10 }, .cdb_length = 10, .direction = Direction::In, .data_length = ReadCapacity10DataSize };
        R_TRY(this->Execute(cmd10, data));

        u64 last_lba   = LoadBe<u32>(data + 0);
        u32 block_size = LoadBe<u32>(data + 4);

        if (last_lba == std::numeric_limits<u32>::max()) {
            Command cmd16 = { .cdb = { ScsiReadCapacity16, ServiceActionReadCapacity16 }, .cdb_length = 16, .direction = Direction::In, .data_length = ReadCapacity16DataSize };
            StoreBe<u32>(cmd16.cdb + 10, ReadCapacity16DataSize);
            R_TRY(this->Execute(cmd16, data));

            last_lba   = LoadBe<u64>(data + 0);
            block_size = LoadBe<u32>(data + 8);
        }

        R_UNLESS(IsSupportedBlockSize(block_size), usbl::ResultUnsupportedBlockSize());

        m_block_size  = block_size;
        m_block_count = last_lba + 1;
        R_SUCCEED();
    }

    Result MassStorageDevice::Inquiry(InquiryData *out) {
        u8 data[InquiryDataSize] = {};

        Command cmd = { .cdb = { ScsiInquiry }, .cdb_length = 6, .direction = Direction::In, .data_length = InquiryDataSize };
        cmd.cdb[4] = InquiryDataSize;
        R_TRY(this->Execute(cmd, data));

        CopyTrimmedAscii(out->vendor,   data + 8,  8);
        CopyTrimmedAscii(out->product,  data + 16, 16);
        CopyTrimmedAscii(out->revision, data + 32, 4);
        R_SUCCEED();
    }

    Result MassStorageDevice::Read(void *dst, u64 lba, u32 block_count) {
        R_UNLESS(lba + block_count <= m_block_count && lba + block_count >= lba, usbl::ResultOutOfRange());

        u8 *cur = static_cast<u8 *>(dst);

        while (block_count > 0) {
            const u32 cur_blocks = std::min(block_count, m_max_transfer_size / m_block_size);
            const u32 cur_size   = cur_blocks * m_block_size;

            Command cmd = { .cdb = {}, .cdb_length = 0, .direction = Direction::In, .data_length = cur_size };
            if (lba + cur_blocks <= std::numeric_limits<u32>::max()) {
                cmd.cdb[0]     = ScsiRead10;
                cmd.cdb_length = 10;
                StoreBe<u32>(cmd.cdb + 2, static_cast<u32>(lba));
                StoreBe<u16>(cmd.cdb + 7, static_cast<u16>(cur_blocks));
            } else {
                cmd.cdb[0]     = ScsiRead16;
                cmd.cdb_length = 16;
                StoreBe<u64>(cmd.cdb + 2, lba);
                StoreBe<u32>(cmd.cdb + 10, cur_blocks);
            }

            u32 transferred = 0;
            if (const Result result = this->Execute(cmd, cur, std::addressof(transferred)); R_FAILED(result) || transferred != cur_size) {
                if (cur_size <= SafeTransferSize) {
                    R_TRY(result);
                    R_THROW(usbl::ResultShortTransfer());
                }
                Log("usb: %u KB reads failed (2%03d-%04d), using %zu KB", cur_size / 1024, result.GetModule(), result.GetDescription(), SafeTransferSize / 1024);
                m_max_transfer_size = SafeTransferSize;
                continue;
            }

            cur         += cur_size;
            lba         += cur_blocks;
            block_count -= cur_blocks;
        }

        R_SUCCEED();
    }

    Result MassStorageDevice::Execute(const Command &cmd, void *data, u32 *out_transferred) {
        u32 transferred = 0;
        const Result result = this->ExecuteOnce(cmd, data, std::addressof(transferred));

        if (usbl::ResultCommandFailed::Includes(result)) {
            R_DISCARD(this->RequestSense());
            R_THROW(result);
        }

        if (R_FAILED(result)) {
            R_TRY(this->ResetRecovery());
            R_TRY(this->ExecuteOnce(cmd, data, std::addressof(transferred)));
        }

        if (out_transferred != nullptr) {
            *out_transferred = transferred;
        }
        R_SUCCEED();
    }

    Result MassStorageDevice::ExecuteOnce(const Command &cmd, void *data, u32 *out_transferred) {
        AMS_ABORT_UNLESS(cmd.data_length <= TransferBufferSize);

        const u32 tag = ++m_tag;

        std::memset(m_command_buffer, 0, CommandBlockWrapperSize);
        StoreLe32(m_command_buffer + 0, CommandBlockWrapperSignature);
        StoreLe32(m_command_buffer + 4, tag);
        StoreLe32(m_command_buffer + 8, cmd.data_length);
        m_command_buffer[12] = static_cast<u8>(cmd.direction);
        m_command_buffer[13] = m_lun;
        m_command_buffer[14] = cmd.cdb_length;
        std::memcpy(m_command_buffer + 15, cmd.cdb, cmd.cdb_length);

        u32 sent = 0;
        R_TRY(PostBuffer(std::addressof(m_out_endpoint), m_command_buffer, CommandBlockWrapperSize, std::addressof(sent)));
        R_UNLESS(sent == CommandBlockWrapperSize, usbl::ResultShortTransfer());

        u32 transferred = 0;
        if (cmd.data_length > 0) {
            const bool is_in = cmd.direction == Direction::In;
            auto *ep = is_in ? std::addressof(m_in_endpoint) : std::addressof(m_out_endpoint);

            if (!is_in) {
                std::memcpy(m_transfer_buffer, data, cmd.data_length);
            }

            /* A stalled data phase is still followed by a status phase once the halt is cleared. */
            if (R_FAILED(PostBuffer(ep, m_transfer_buffer, cmd.data_length, std::addressof(transferred)))) {
                transferred = 0;
                R_TRY(this->ClearHalt(*ep));
            } else if (is_in) {
                std::memcpy(data, m_transfer_buffer, transferred);
            }
        }

        R_TRY(this->ReceiveStatus(tag));

        *out_transferred = transferred;
        R_SUCCEED();
    }

    Result MassStorageDevice::ReceiveStatus(u32 tag) {
        u32 received = 0;
        if (R_FAILED(PostBuffer(std::addressof(m_in_endpoint), m_status_buffer, CommandStatusWrapperSize, std::addressof(received)))) {
            R_TRY(this->ClearHalt(m_in_endpoint));
            R_TRY(PostBuffer(std::addressof(m_in_endpoint), m_status_buffer, CommandStatusWrapperSize, std::addressof(received)));
        }

        R_UNLESS(received == CommandStatusWrapperSize,                         usbl::ResultInvalidCommandStatus());
        R_UNLESS(LoadLe32(m_status_buffer + 0) == CommandStatusWrapperSignature, usbl::ResultInvalidCommandStatus());
        R_UNLESS(LoadLe32(m_status_buffer + 4) == tag,                         usbl::ResultInvalidCommandStatus());

        switch (m_status_buffer[12]) {
            case CommandStatusPassed:     R_SUCCEED();
            case CommandStatusFailed:     R_THROW(usbl::ResultCommandFailed());
            case CommandStatusPhaseError: R_THROW(usbl::ResultPhaseError());
            default:                      R_THROW(usbl::ResultInvalidCommandStatus());
        }
    }

    Result MassStorageDevice::RequestSense() {
        u8 data[RequestSenseDataSize] = {};

        Command cmd = { .cdb = { ScsiRequestSense }, .cdb_length = 6, .direction = Direction::In, .data_length = RequestSenseDataSize };
        cmd.cdb[4] = RequestSenseDataSize;

        u32 transferred = 0;
        R_RETURN(this->ExecuteOnce(cmd, data, std::addressof(transferred)));
    }

    Result MassStorageDevice::ResetRecovery() {
        const u16 interface_number = m_interface_session.inf.inf.interface_desc.bInterfaceNumber;

        u32 transferred = 0;
        R_TRY(::usbHsIfCtrlXfer(std::addressof(m_interface_session), RequestTypeClassInterfaceOut, RequestBulkOnlyReset, 0, interface_number, 0, m_control_buffer, std::addressof(transferred)));
        R_TRY(this->ClearHalt(m_in_endpoint));
        R_TRY(this->ClearHalt(m_out_endpoint));
        R_SUCCEED();
    }

    Result MassStorageDevice::ClearHalt(const ::UsbHsClientEpSession &ep) {
        u32 transferred = 0;
        R_RETURN(::usbHsIfCtrlXfer(std::addressof(m_interface_session), RequestTypeStandardEndpointOut, RequestClearFeature, FeatureEndpointHalt, ep.desc.bEndpointAddress, 0, m_control_buffer, std::addressof(transferred)));
    }

}
