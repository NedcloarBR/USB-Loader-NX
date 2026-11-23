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
#include <usbl_client.hpp>
#include <algorithm>
#include <cstring>

namespace usbl {

    namespace {

        constexpr size_t MaxDrives = 8;
        constexpr size_t MaxGames  = 4096;

        Service g_service;
        bool g_overwrite_newer_config = false;
        u16 g_module_minor_version = 0;

        constexpr u32 ToCommand(protocol::CommandId id) {
            return static_cast<u32>(id);
        }

        /* The library is polled every 1.5 s: the buffer starts small and grows only while the list fills it, instead of
         * mapping room for every possible game on each call. */
        constexpr size_t InitialListCount = 64;

        template<typename T>
        Result List(std::vector<T> *out, protocol::CommandId command_id, size_t max_count) {
            size_t capacity = std::min(InitialListCount, max_count);
            while (true) {
                out->resize(capacity);

                s32 count = 0;
                const Result rc = serviceDispatchOut(&g_service, ToCommand(command_id), count,
                    .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
                    .buffers = { { out->data(), out->size() * sizeof(T) } },
                );
                if (R_FAILED(rc)) {
                    out->clear();
                    return rc;
                }
                if (static_cast<size_t>(count) < capacity || capacity == max_count) {
                    out->resize(static_cast<size_t>(count));
                    return rc;
                }
                capacity = std::min(capacity * 4, max_count);
            }
        }

    }

    Result Initialize() {
        /* sm holds a request for a service nobody registered until someone does: with USB Loader NX off that is never,
         * and the Manager would wait on a black screen. Atmosphère's sm answers whether it exists (command 65100). */
        const SmServiceName name = smEncodeName(protocol::ServiceName);
        u8 has_service = 0;
        if (const Result rc = tipcDispatchInOut(smGetServiceSessionTipc(), 65100, name, has_service); R_FAILED(rc)) {
            return rc;
        }
        if ((has_service & 1) == 0) {
            return MAKERESULT(Module_Libnx, LibnxError_NotFound);
        }
        return smGetService(&g_service, protocol::ServiceName);
    }

    void Exit() {
        serviceClose(&g_service);
    }

    Result GetConfigValue(ConfigValue *out, ConfigKey key) {
        const u8 in = static_cast<u8>(key);
        return serviceDispatchInOut(&g_service, ToCommand(protocol::CommandId::GetConfigValue), in, *out);
    }

    Result GetVersion(VersionInfo *out) {
        const Result rc = serviceDispatchOut(&g_service, ToCommand(protocol::CommandId::GetVersion), *out);
        if (R_SUCCEEDED(rc)) {
            g_module_minor_version = out->minor;
        }
        return rc;
    }

    u16 GetModuleMinorVersion() {
        return g_module_minor_version;
    }

    Result SetConfigValue(ConfigKey key, bool enabled) {
        const protocol::SetConfigValueInput in = { .key = key, .enabled = enabled, .overwrite = g_overwrite_newer_config, .reserved = {} };
        return serviceDispatchIn(&g_service, ToCommand(protocol::CommandId::SetConfigValue), in);
    }

    Result GetLanguage(std::string *out) {
        protocol::Language language = {};
        const Result rc = serviceDispatchOut(&g_service, ToCommand(protocol::CommandId::GetLanguage), language);
        if (R_SUCCEEDED(rc)) {
            *out = std::string(language.locale, strnlen(language.locale, sizeof(language.locale)));
        }
        return rc;
    }

    Result SetLanguage(const std::string &locale) {
        protocol::SetLanguageInput in = { .language = {}, .overwrite = g_overwrite_newer_config, .reserved = {} };
        std::memcpy(in.language.locale, locale.data(), std::min(locale.size(), sizeof(in.language.locale) - 1));
        return serviceDispatchIn(&g_service, ToCommand(protocol::CommandId::SetLanguage), in);
    }

    void SetOverwriteNewerConfig(bool overwrite) {
        g_overwrite_newer_config = overwrite;
    }

    Result ListDrives(std::vector<DriveStatus> *out) {
        return List(out, protocol::CommandId::ListDrives, MaxDrives);
    }

    Result ListGames(std::vector<GameStatus> *out) {
        return List(out, protocol::CommandId::ListGames, MaxGames);
    }

    Result SetGameHidden(u64 application_id, bool hidden) {
        const protocol::SetGameHiddenInput in = { application_id, hidden, {} };
        return serviceDispatchIn(&g_service, ToCommand(protocol::CommandId::SetGameHidden), in);
    }

    Result ShowGameForLaunch(u64 application_id) {
        return serviceDispatchIn(&g_service, ToCommand(protocol::CommandId::ShowGameForLaunch), application_id);
    }

    Result FinishHomeRefresh() {
        if (!serviceIsActive(&g_service)) {
            return 0;
        }
        return serviceDispatch(&g_service, ToCommand(protocol::CommandId::FinishHomeRefresh));
    }

    Result ApplyHomeVisibility() {
        return serviceDispatch(&g_service, ToCommand(protocol::CommandId::ApplyHomeVisibility));
    }

    Result GetGameDisplayVersion(u64 application_id, std::string *out) {
        protocol::DisplayVersion version = {};
        const Result rc = serviceDispatchInOut(&g_service, ToCommand(protocol::CommandId::GetGameDisplayVersion), application_id, version);
        if (R_SUCCEEDED(rc)) {
            out->assign(version.text, strnlen(version.text, sizeof(version.text)));
        }
        return rc;
    }

    Result GetGameIcon(u64 application_id, std::vector<u8> *out) {
        out->resize(protocol::MaxIconSize);
        u32 size = 0;
        const Result rc = serviceDispatchInOut(&g_service, ToCommand(protocol::CommandId::GetGameIcon), application_id, size,
            .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
            .buffers = { { out->data(), out->size() } },
        );
        out->resize(R_SUCCEEDED(rc) ? std::min<size_t>(size, out->size()) : 0);
        return rc;
    }

}
