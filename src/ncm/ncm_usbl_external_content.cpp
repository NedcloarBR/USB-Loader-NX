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
#include "ncm_usbl_external_content.hpp"
#include "ncm_usbl_shim.h"

namespace ams::ncm {

    namespace {

        constexpr sm::ServiceName UsblServiceName = sm::ServiceName::Encode("usbl");

        constinit os::SdkMutex g_usbl_lock;
        constinit ::Service g_usbl_service = {};

        /* The caller's buffer is usually itself an IPC mapping from ns, which cannot be re-mapped into another request. */
        constexpr size_t ReadBounceBufferSize = 16_KB;
        alignas(os::MemoryPageSize) constinit u8 g_read_bounce_buffer[ReadBounceBufferSize];

        /* ncm starts before ams.mitm registers "usbl" (and it may be disabled); sm defers requests for unregistered services, so never block here. */
        bool EnsureUsblSession() {
            if (::serviceIsActive(std::addressof(g_usbl_service))) {
                return true;
            }

            bool has_service = false;
            if (R_FAILED(sm::HasService(std::addressof(has_service), UsblServiceName)) || !has_service) {
                return false;
            }

            os::NativeHandle handle;
            if (R_FAILED(sm::GetServiceHandle(std::addressof(handle), UsblServiceName))) {
                return false;
            }

            ::serviceCreate(std::addressof(g_usbl_service), handle);
            return true;
        }

    }

    bool FindUsblContent(ExternalContentInfo *out, ContentId content_id, ExternalContentQuery query) {
        std::scoped_lock lk(g_usbl_lock);

        if (!EnsureUsblSession()) {
            return false;
        }

        static_assert(sizeof(::NcmContentId) == sizeof(ContentId));
        ::UsblContentQuery request = {};
        std::memcpy(std::addressof(request.content_id), std::addressof(content_id), sizeof(content_id));
        request.query = static_cast<u32>(query);

        ::UsblServedContentInfo info;
        if (R_FAILED(::usblGetContentInfoForNcm(std::addressof(g_usbl_service), std::addressof(info), std::addressof(request)))) {
            /* USB Loader NX went away; reconnect on the next query. */
            ::serviceClose(std::addressof(g_usbl_service));
            return false;
        }

        if (!info.is_served) {
            return false;
        }

        static_assert(sizeof(info.rights_id) == sizeof(out->rights_id));
        out->size           = info.size;
        out->program_id     = { info.program_id };
        out->key_generation = info.key_generation;
        std::memcpy(std::addressof(out->rights_id), info.rights_id, sizeof(info.rights_id));
        return true;
    }

    Result ReadUsblContent(void *dst, size_t size, ContentId content_id, s64 offset) {
        std::scoped_lock lk(g_usbl_lock);
        R_UNLESS(EnsureUsblSession(), ncm::ResultContentNotFound());

        ::UsblContentReadRequest request = {};
        std::memcpy(std::addressof(request.content_id), std::addressof(content_id), sizeof(content_id));

        u8 *cur = static_cast<u8 *>(dst);
        while (size > 0) {
            const size_t cur_size = std::min(size, ReadBounceBufferSize);
            request.offset = offset;
            R_TRY(::usblReadNcaContent(std::addressof(g_usbl_service), g_read_bounce_buffer, cur_size, std::addressof(request)));
            std::memcpy(cur, g_read_bounce_buffer, cur_size);

            cur    += cur_size;
            offset += cur_size;
            size   -= cur_size;
        }
        R_SUCCEED();
    }

    bool ForgetUsblContent(ContentId content_id) {
        std::scoped_lock lk(g_usbl_lock);

        if (!EnsureUsblSession()) {
            return false;
        }

        ::NcmContentId request;
        std::memcpy(std::addressof(request), std::addressof(content_id), sizeof(content_id));

        bool was_known = false;
        if (R_FAILED(::usblForgetContentForNcm(std::addressof(g_usbl_service), std::addressof(was_known), std::addressof(request)))) {
            ::serviceClose(std::addressof(g_usbl_service));
            return false;
        }
        return was_known;
    }

}
