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

namespace ams::mitm::usbl::xci {

    /* Routes ncm's content meta mounting to the USB content index. */
    void InitializeContentMetaReader();

    /* Reads the packaged content meta (.cnmt) from the program's Meta NCA, using ncm's own reader. */
    Result ReadPackagedContentMeta(ncm::AutoBuffer *out, ncm::ProgramId program_id);

    void LogIndexedContentMetas();

}
