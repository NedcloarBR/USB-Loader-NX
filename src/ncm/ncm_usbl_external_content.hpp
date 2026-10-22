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

namespace ams::ncm {

    /* Answers ncm::FindExternalContent by asking USB Loader NX, once its "usbl" service exists. */
    bool FindUsblContent(ExternalContentInfo *out, ContentId content_id, ExternalContentQuery query);
    Result ReadUsblContent(void *dst, size_t size, ContentId content_id, s64 offset);
    bool ForgetUsblContent(ContentId content_id);

}
