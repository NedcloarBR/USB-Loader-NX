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
#include "usbl_badge.hpp"
#include "../usbl_config.hpp"

extern "C" {
#include "../third_party/stb/stb_image.h"
#include "../third_party/stb/stb_image_write.h"

    extern const u8 usbloadernx_badge_xci_rgba[];
    extern const u8 usbloadernx_badge_xci_rgba_end[];
    extern const u8 usbloadernx_badge_xcz_rgba[];
    extern const u8 usbloadernx_badge_xcz_rgba_end[];
    extern const u8 usbloadernx_badge_nsz_rgba[];
    extern const u8 usbloadernx_badge_nsz_rgba_end[];
    extern const u8 usbloadernx_badge_nsp_rgba[];
    extern const u8 usbloadernx_badge_nsp_rgba_end[];
    extern const u8 usbloadernx_badge_installed_rgba[];
    extern const u8 usbloadernx_badge_installed_rgba_end[];
}

namespace ams::mitm::usbl::badge {

    namespace {

        /* The badge asset is laid out for a 256x256 icon: u16 x, y, width, height, then RGBA8888 rows. */
        constexpr s32 ReferenceIconSize = 256;
        constexpr int JpegQuality = 92;

        struct BadgeHeader {
            u16 x, y, width, height;
        };
        static_assert(sizeof(BadgeHeader) == 8);

        constinit os::SdkMutex g_stb_lock;

        struct EncodeBuffer {
            std::unique_ptr<u8[]> data;
            size_t size;
            size_t capacity;
            bool failed;
        };

        void AppendToBuffer(void *context, void *src, int size) {
            auto *buffer = static_cast<EncodeBuffer *>(context);
            if (buffer->failed || size <= 0) {
                return;
            }

            if (buffer->size + size > buffer->capacity) {
                const size_t new_capacity = std::max(buffer->capacity * 2, buffer->size + size);
                auto grown = std::unique_ptr<u8[]>(new (std::nothrow) u8[new_capacity]);
                if (grown == nullptr) {
                    buffer->failed = true;
                    return;
                }
                std::memcpy(grown.get(), buffer->data.get(), buffer->size);
                buffer->data = std::move(grown);
                buffer->capacity = new_capacity;
            }

            std::memcpy(buffer->data.get() + buffer->size, src, size);
            buffer->size += size;
        }

        /* Straight-alpha source over an opaque RGB destination; the badge is scaled to the icon by nearest sampling. */
        /* A malformed asset is a build problem; it leaves the icon untouched instead of taking ams.mitm down. */
        struct BadgeAsset {
            const u8 *begin;
            const u8 *end;
        };

        BadgeAsset GetBadgeAsset(BadgeSource badge) {
            switch (badge.source) {
                case ContentSource::Nsp:
                    return badge.is_compressed ? BadgeAsset{ usbloadernx_badge_nsz_rgba, usbloadernx_badge_nsz_rgba_end } : BadgeAsset{ usbloadernx_badge_nsp_rgba, usbloadernx_badge_nsp_rgba_end };
                case ContentSource::Installed: return { usbloadernx_badge_installed_rgba, usbloadernx_badge_installed_rgba_end };
                case ContentSource::Xci:
                default:
                    return badge.is_compressed ? BadgeAsset{ usbloadernx_badge_xcz_rgba, usbloadernx_badge_xcz_rgba_end } : BadgeAsset{ usbloadernx_badge_xci_rgba, usbloadernx_badge_xci_rgba_end };
            }
        }

        bool Composite(u8 *rgb, s32 icon_size, BadgeSource source) {
            const BadgeAsset asset = GetBadgeAsset(source);

            BadgeHeader header;
            std::memcpy(std::addressof(header), asset.begin, sizeof(header));
            const u8 *badge = asset.begin + sizeof(header);
            if (static_cast<size_t>(asset.end - badge) != static_cast<size_t>(header.width) * header.height * 4) {
                return false;
            }

            const s32 x0 = header.x * icon_size / ReferenceIconSize;
            const s32 y0 = header.y * icon_size / ReferenceIconSize;
            const s32 x1 = (header.x + header.width) * icon_size / ReferenceIconSize;
            const s32 y1 = (header.y + header.height) * icon_size / ReferenceIconSize;

            for (s32 y = y0; y < y1 && y < icon_size; ++y) {
                const s32 by = (y - y0) * header.height / std::max(1, y1 - y0);
                for (s32 x = x0; x < x1 && x < icon_size; ++x) {
                    const s32 bx = (x - x0) * header.width / std::max(1, x1 - x0);
                    const u8 *src = badge + (by * header.width + bx) * 4;
                    const u32 alpha = src[3];
                    if (alpha == 0) {
                        continue;
                    }

                    u8 *dst = rgb + (y * icon_size + x) * 3;
                    for (int c = 0; c < 3; ++c) {
                        dst[c] = static_cast<u8>((src[c] * alpha + dst[c] * (255 - alpha) + 127) / 255);
                    }
                }
            }
            return true;
        }

    }

    bool IsEnabled() {
        return IsConfigEnabled(ConfigKey::Badge);
    }

    Result ApplyToJpeg(std::unique_ptr<u8[]> *out, size_t *out_size, const void *jpeg, size_t jpeg_size, BadgeSource source) {
        R_UNLESS(jpeg_size <= static_cast<size_t>(std::numeric_limits<int>::max()), ams::fs::ResultInvalidArgument());

        std::scoped_lock lk(g_stb_lock);

        int width = 0, height = 0, components = 0;
        u8 *rgb = stbi_load_from_memory(static_cast<const stbi_uc *>(jpeg), static_cast<int>(jpeg_size), std::addressof(width), std::addressof(height), std::addressof(components), 3);
        R_UNLESS(rgb != nullptr, ams::fs::ResultInvalidArgument());
        ON_SCOPE_EXIT { stbi_image_free(rgb); };
        R_UNLESS(width == height && width > 0, ams::fs::ResultInvalidArgument());

        R_UNLESS(Composite(rgb, width, source), ams::fs::ResultInvalidArgument());

        EncodeBuffer buffer = { .data = std::unique_ptr<u8[]>(new (std::nothrow) u8[jpeg_size + 16_KB]), .size = 0, .capacity = jpeg_size + 16_KB, .failed = false };
        R_UNLESS(buffer.data != nullptr, ams::fs::ResultAllocationMemoryFailedNew());

        R_UNLESS(stbi_write_jpg_to_func(AppendToBuffer, std::addressof(buffer), width, height, 3, rgb, JpegQuality) != 0 && !buffer.failed, ams::fs::ResultAllocationMemoryFailedNew());

        *out = std::move(buffer.data);
        *out_size = buffer.size;
        R_SUCCEED();
    }

}
