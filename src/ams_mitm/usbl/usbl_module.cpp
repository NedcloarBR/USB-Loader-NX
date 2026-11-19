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
#include "../amsmitm_initialization.hpp"
#include "i18n/usbl_i18n.hpp"
#include "nca/usbl_nca_crypto.hpp"
#include "nca/usbl_nca_filesystem.hpp"
#include "xci/usbl_content_meta.hpp"
#include "xci/usbl_registration.hpp"
#include "usbl_log.hpp"
#include "usbl_module.hpp"
#include "usbl_config.hpp"
#include "usbl_notifications.hpp"
#include "usbl_service.hpp"
#include "usbl_power_state.hpp"
#include "usbl_usb_volume_manager.hpp"
#include "xci/usbl_hidden_games.hpp"
#include "xci/usbl_known_contents.hpp"

namespace ams::mitm::usbl {

    namespace {

        /* IFileSystem paths arrive as pointer buffers; without a receive buffer the server retries the receive forever. */
        struct ServerOptions {
            static constexpr size_t PointerBufferSize   = 0x400;
            static constexpr size_t MaxDomains          = 0;
            static constexpr size_t MaxDomainObjects    = 0;
            static constexpr bool CanDeferInvokeRequest = false;
            static constexpr bool CanManageMitmServers  = false;
        };

        constexpr sm::ServiceName ServiceName = sm::ServiceName::Encode("usbl");
        constexpr size_t          MaxServiceSessions = 4;

        /* Every ExeFS handed to loader occupies a session of its own (three per launch). */
        constexpr size_t NumServers  = 1;
        constexpr size_t NumSessions = MaxServiceSessions + 12;

        sf::hipc::ServerManager<NumServers, ServerOptions, NumSessions> g_server_manager;

        constinit sf::UnmanagedServiceObject<impl::IService, Service> g_service_object;

        /* A long ExeFS read by loader must never stall the quick queries ncm makes on behalf of ns. */
        constexpr size_t ExtraServerThreadCount = 3;
        constexpr size_t ServerThreadStackSize  = 32_KB;
        alignas(os::ThreadStackAlignment) constinit u8 g_server_thread_stacks[ExtraServerThreadCount][ServerThreadStackSize];
        constinit os::ThreadType g_server_threads[ExtraServerThreadCount];

        bool IsEnabled() {
            u8 enabled = 0;
            return ams::settings::fwdbg::GetSettingsItemValue(std::addressof(enabled), sizeof(enabled), "usbloadernx", "enable") == sizeof(enabled) && enabled != 0;
        }

        void ServerThreadFunction(void *) {
            g_server_manager.LoopProcess();
        }

    }

    void MitmModule::ThreadFunction(void *) {
        /* Wait until the SD card is mounted. */
        mitm::WaitInitialized();

        if (!IsEnabled()) {
            return;
        }

        InitializeLog();
        Log("usbloadernx starting (build " __DATE__ " " __TIME__ ")");

        nca::InitializeCrypto();
        nca::InitializeFileSystem();
        xci::InitializeContentMetaReader();
        Log("nca crypto ready");

        LoadConfig();
        i18n::Initialize();
        InitializeNotifications();
        LoadKnownContents();
        LoadHiddenGames();

        StartPowerStateMonitor();
        StartUsbVolumeManager();
        xci::StartLaunchWatch();
        xci::StartHomeRefreshThread();

        R_ABORT_UNLESS(g_server_manager.RegisterObjectForServer(g_service_object.GetShared(), ServiceName, MaxServiceSessions));
        Log("service usbl registered");

        for (size_t i = 0; i < ExtraServerThreadCount; ++i) {
            R_ABORT_UNLESS(os::CreateThread(g_server_threads + i, ServerThreadFunction, nullptr, g_server_thread_stacks[i], ServerThreadStackSize, AMS_GET_SYSTEM_THREAD_PRIORITY(mitm_usbl, IpcServer)));
            os::SetThreadNamePointer(g_server_threads + i, AMS_GET_SYSTEM_THREAD_NAME(mitm_usbl, IpcServer));
            os::StartThread(g_server_threads + i);
        }

        g_server_manager.LoopProcess();
    }

}
