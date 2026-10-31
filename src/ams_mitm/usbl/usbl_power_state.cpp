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
#include "usbl_power_state.hpp"
#include "usbl_log.hpp"

namespace ams::mitm::usbl {

    namespace {

        /* Not a firmware module id; psc only needs it unique. Depending on Usb gets us the sleep request before the host controller powers down. */
        constexpr psc::PmModuleId UsblPmModuleId = static_cast<psc::PmModuleId>(0x5542);
        constexpr psc::PmModuleId PmModuleDependencies[] = { psc::PmModuleId_Usb };

        /* Re-enumeration after wake takes a few seconds; detaches seen that late still belong to the sleep. */
        constexpr TimeSpan WakeGracePeriod = TimeSpan::FromSeconds(15);

        constexpr size_t ThreadStackSize = 4_KB;
        alignas(os::ThreadStackAlignment) constinit u8 g_thread_stack[ThreadStackSize];
        constinit os::ThreadType g_thread;

        psc::PmModule g_pm_module;

        constinit std::atomic<bool> g_is_asleep = false;
        constinit std::atomic<s64> g_awake_tick = 0;

        Result RegisterPmModule() {
            /* psc may come up after ams.mitm; this blocks until it does, so it runs on this thread. */
            R_TRY(::pscmInitialize());
            R_RETURN(g_pm_module.Initialize(UsblPmModuleId, PmModuleDependencies, util::size(PmModuleDependencies), os::EventClearMode_AutoClear));
        }

        void PowerStateThreadFunction(void *) {
            /* Without psc every detach is reported as a removal, which is only cosmetic. */
            if (const Result result = RegisterPmModule(); R_FAILED(result)) {
                Log("psc module not registered 2%03d-%04d", result.GetModule(), result.GetDescription());
                return;
            }

            auto *event = g_pm_module.GetEventPointer();
            while (true) {
                event->Wait();

                psc::PmState state;
                psc::PmFlagSet flags;
                if (R_FAILED(g_pm_module.GetRequest(std::addressof(state), std::addressof(flags)))) {
                    continue;
                }

                if (state == psc::PmState_FullAwake) {
                    g_awake_tick = os::GetSystemTick().GetInt64Value();
                    g_is_asleep = false;
                } else {
                    g_is_asleep = true;
                }

                R_ABORT_UNLESS(g_pm_module.Acknowledge(state, ResultSuccess()));
            }
        }

    }

    void StartPowerStateMonitor() {
        R_ABORT_UNLESS(os::CreateThread(std::addressof(g_thread), PowerStateThreadFunction, nullptr, g_thread_stack, sizeof(g_thread_stack), AMS_GET_SYSTEM_THREAD_PRIORITY(mitm_usbl, PowerState)));
        os::SetThreadNamePointer(std::addressof(g_thread), AMS_GET_SYSTEM_THREAD_NAME(mitm_usbl, PowerState));
        os::StartThread(std::addressof(g_thread));
    }

    bool IsInSleepTransition() {
        if (g_is_asleep) {
            return true;
        }

        const s64 awake_tick = g_awake_tick;
        return awake_tick != 0 && os::ConvertToTimeSpan(os::Tick(os::GetSystemTick().GetInt64Value() - awake_tick)) < WakeGracePeriod;
    }

}
