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

    /* Titlekeys of eShop content. ns obtains each one from es and hands it to FS (fsp-srv RegisterExternalKey);
     * fs_mitm keeps a copy here so content served from USB can be decrypted the same way. */
    void RegisterExternalKey(const ams::fs::RightsId &rights_id, const spl::AccessKey &access_key);
    void UnregisterAllExternalKeys();

    /* Gives the reader its titlekey when the NCA has a rights id; NCAs without one need nothing. */
    Result ApplyExternalKey(fssystem::NcaReader &reader);

}
