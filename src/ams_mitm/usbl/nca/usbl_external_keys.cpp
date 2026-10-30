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
#include "usbl_external_keys.hpp"

namespace ams::mitm::usbl::nca {

    namespace {

        /* ns registers the keys of one application at a time; a handful of slots covers an app with its update and DLC. */
        constexpr size_t MaxExternalKeys = 32;

        struct ExternalKey {
            ams::fs::RightsId rights_id;
            spl::AccessKey access_key;
        };

        constinit os::SdkMutex g_lock;
        constinit ExternalKey g_keys[MaxExternalKeys] = {};
        constinit size_t g_key_count = 0;
        constinit size_t g_next_replaced = 0;

        bool FindExternalKey(spl::AccessKey *out, const ams::fs::RightsId &rights_id) {
            std::scoped_lock lk(g_lock);

            for (size_t i = 0; i < g_key_count; ++i) {
                if (g_keys[i].rights_id == rights_id) {
                    *out = g_keys[i].access_key;
                    return true;
                }
            }
            return false;
        }

    }

    void RegisterExternalKey(const ams::fs::RightsId &rights_id, const spl::AccessKey &access_key) {
        std::scoped_lock lk(g_lock);

        for (size_t i = 0; i < g_key_count; ++i) {
            if (g_keys[i].rights_id == rights_id) {
                g_keys[i].access_key = access_key;
                return;
            }
        }

        const size_t index = g_key_count < MaxExternalKeys ? g_key_count++ : std::exchange(g_next_replaced, (g_next_replaced + 1) % MaxExternalKeys);
        g_keys[index] = { .rights_id = rights_id, .access_key = access_key };
    }

    void UnregisterAllExternalKeys() {
        std::scoped_lock lk(g_lock);

        std::memset(g_keys, 0, sizeof(g_keys));
        g_key_count = 0;
        g_next_replaced = 0;
    }

    Result ApplyExternalKey(fssystem::NcaReader &reader) {
        ams::fs::RightsId rights_id;
        reader.GetRightsId(rights_id.data, sizeof(rights_id.data));
        R_SUCCEED_IF(rights_id == ams::fs::InvalidRightsId);

        spl::AccessKey access_key;
        R_UNLESS(FindExternalKey(std::addressof(access_key), rights_id), ams::fs::ResultNcaExternalKeyUnregistered());

        reader.SetExternalDecryptionKey(std::addressof(access_key), sizeof(access_key));
        R_SUCCEED();
    }

}
