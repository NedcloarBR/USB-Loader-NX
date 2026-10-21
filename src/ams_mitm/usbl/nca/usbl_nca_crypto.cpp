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
#include "usbl_nca_crypto.hpp"

namespace ams::mitm::usbl::nca {

    namespace {

        using Config = fssystem::NcaCryptoConfiguration;

        constexpr size_t KeySize = Config::Aes128KeySize;

        constinit Config g_crypto_configuration = {};
        constinit os::SdkMutex g_access_key_lock;

        constinit spl::AccessKey g_header_access_key = {};
        constinit spl::AccessKey g_key_area_access_keys[fssystem::KeyAreaEncryptionKeyCount] = {};
        constinit bool g_key_area_access_key_valid[fssystem::KeyAreaEncryptionKeyCount] = {};
        constinit bool g_key_generation_failed = false;

        constinit fssystem::VerifySign1Function g_verify_nintendo_sign1 = nullptr;
        constinit bool g_resigned_header_accepted = false;

        /* Repacked dumps (e.g. DBI's) re-sign the NCA header with a key that is not Nintendo's. Like a sigpatched FS, */
        /* USB content is accepted anyway; the content itself is still hash-checked. */
        bool VerifySign1(const void *sig, size_t sig_size, const void *data, size_t data_size, u8 generation) {
            if (!g_verify_nintendo_sign1(sig, sig_size, data, data_size, generation)) {
                g_resigned_header_accepted = true;
            }
            return true;
        }

        bool IsHeaderKeyType(s32 key_type) {
            return key_type == util::ToUnderlying(fssystem::KeyType::NcaHeaderKey1) || key_type == util::ToUnderlying(fssystem::KeyType::NcaHeaderKey2);
        }

        /* Mirrors fssystem::SetUpKekAccessKeys, but lazily and without reserving any of the 9 system-wide virtual keyslots. */
        Result GetKeyAreaAccessKey(spl::AccessKey *out, s32 key_type) {
            std::scoped_lock lk(g_access_key_lock);

            if (!g_key_area_access_key_valid[key_type]) {
                const s32 key_index  = key_type % Config::KeyAreaEncryptionKeyIndexCount;
                const s32 generation = key_type / Config::KeyAreaEncryptionKeyIndexCount;
                R_TRY(spl::GenerateAesKek(std::addressof(g_key_area_access_keys[key_type]), g_crypto_configuration.key_area_encryption_key_source[key_index], KeySize, generation, 0));
                g_key_area_access_key_valid[key_type] = true;
            }

            *out = g_key_area_access_keys[key_type];
            R_SUCCEED();
        }

        Result GenerateNcaKeyImpl(void *dst, size_t dst_size, const void *src, size_t src_size, s32 key_type) {
            if (IsHeaderKeyType(key_type)) {
                R_RETURN(spl::GenerateAesKey(dst, dst_size, g_header_access_key, src, src_size));
            }

            R_UNLESS(!fssystem::IsInvalidKeyTypeValue(key_type) && key_type < fssystem::KeyAreaEncryptionKeyCount, spl::ResultInvalidArgument());

            spl::AccessKey access_key;
            R_TRY(GetKeyAreaAccessKey(std::addressof(access_key), key_type));
            R_RETURN(spl::GenerateAesKey(dst, dst_size, access_key, src, src_size));
        }

        /* The fssystem callback cannot fail, so failures (e.g. a key generation newer than this firmware) are recorded instead of aborting. */
        void GenerateNcaKey(void *dst, size_t dst_size, const void *src, size_t src_size, s32 key_type) {
            if (R_FAILED(GenerateNcaKeyImpl(dst, dst_size, src, src_size, key_type))) {
                std::memset(dst, 0, dst_size);
                g_key_generation_failed = true;
            }
        }

    }

    void InitializeCrypto() {
        /* spl:fs is already open: fssystem::InitializeForAtmosphereMitm runs before any module. */
        g_crypto_configuration = *fssystem::GetNcaCryptoConfiguration(!spl::IsDevelopment());
        g_crypto_configuration.generate_key             = GenerateNcaKey;
        /* decrypt_aes_ctr_external (titlekeys) stays: it uses the key slot cache fssystem::InitializeForAtmosphereMitm set up. */
        g_crypto_configuration.decrypt_aes_ctr          = nullptr;

        g_verify_nintendo_sign1                        = g_crypto_configuration.verify_sign1;
        g_crypto_configuration.verify_sign1            = VerifySign1;

        R_ABORT_UNLESS(spl::GenerateAesKek(std::addressof(g_header_access_key), g_crypto_configuration.header_encryption_key_source, KeySize, 0, 0));
    }

    bool ConsumeKeyGenerationFailure() {
        return std::exchange(g_key_generation_failed, false);
    }

    bool ConsumeResignedHeaderAcceptance() {
        return std::exchange(g_resigned_header_accepted, false);
    }

    const fssystem::NcaCryptoConfiguration &GetCryptoConfiguration() {
        return g_crypto_configuration;
    }

}
