/*
 * Copyright (c) Atmosphère-NX
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

namespace ams::ncm {

    namespace {

        constinit FindExternalContentFunction g_find_external_content_func = nullptr;
        constinit ReadExternalContentFunction g_read_external_content_func = nullptr;
        constinit ForgetExternalContentFunction g_forget_external_content_func = nullptr;

    }

    void SetFindExternalContentFunction(FindExternalContentFunction func) {
        g_find_external_content_func = func;
    }

    void SetReadExternalContentFunction(ReadExternalContentFunction func) {
        g_read_external_content_func = func;
    }

    void SetForgetExternalContentFunction(ForgetExternalContentFunction func) {
        g_forget_external_content_func = func;
    }

    bool FindExternalContent(ExternalContentInfo *out, ContentId content_id, ExternalContentQuery query) {
        return g_find_external_content_func != nullptr && g_find_external_content_func(out, content_id, query);
    }

    Result ReadExternalContent(void *dst, size_t size, ContentId content_id, s64 offset) {
        R_UNLESS(g_read_external_content_func != nullptr, ncm::ResultContentNotFound());
        R_RETURN(g_read_external_content_func(dst, size, content_id, offset));
    }

    bool ForgetExternalContent(ContentId content_id) {
        return g_forget_external_content_func != nullptr && g_forget_external_content_func(content_id);
    }

}
