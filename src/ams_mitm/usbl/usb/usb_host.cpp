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
#include "usb_host.hpp"

namespace ams::mitm::usbl::usb {

    namespace {

        constexpr u8 InterfaceClassMassStorage  = 0x08;
        constexpr u8 InterfaceSubClassScsi      = 0x06;
        constexpr u8 InterfaceProtocolBulkOnly  = 0x50;

        constexpr u8 InterfaceAvailableEventIndex = 0;

        constexpr size_t MaxInterfaces = 8;

        /* [7.0.0+] Filters must be unique system-wide; the bcdDevice range matches everything */
        /* but keeps ours distinct from the class-only filter used by homebrew like DBI/nxdumptool. */
        constexpr ::UsbHsInterfaceFilter MassStorageFilter = {
            .Flags              = UsbHsInterfaceFilterFlags_bcdDevice_Min | UsbHsInterfaceFilterFlags_bcdDevice_Max |
                                  UsbHsInterfaceFilterFlags_bInterfaceClass | UsbHsInterfaceFilterFlags_bInterfaceSubClass |
                                  UsbHsInterfaceFilterFlags_bInterfaceProtocol,
            .bcdDevice_Min      = 0x0000,
            .bcdDevice_Max      = 0xFFFF,
            .bInterfaceClass    = InterfaceClassMassStorage,
            .bInterfaceSubClass = InterfaceSubClassScsi,
            .bInterfaceProtocol = InterfaceProtocolBulkOnly,
        };

        constinit ::Event g_interface_available_event = {};
        constinit ::UsbHsInterface g_interfaces[MaxInterfaces] = {};

    }

    Result Initialize() {
        R_TRY(sm::WaitService(sm::ServiceName::Encode("usb:hs")));
        R_TRY(::usbHsInitialize());

        auto filter = MassStorageFilter;
        R_RETURN(::usbHsCreateInterfaceAvailableEvent(std::addressof(g_interface_available_event), true, InterfaceAvailableEventIndex, std::addressof(filter)));
    }

    s32 QueryAvailableInterfaces(::UsbHsInterface *out, s32 max_count) {
        auto filter = MassStorageFilter;

        s32 count = 0;
        if (R_FAILED(::usbHsQueryAvailableInterfaces(std::addressof(filter), out, max_count * sizeof(::UsbHsInterface), std::addressof(count)))) {
            return 0;
        }
        return count;
    }

    bool IsInterfaceAcquired(s32 interface_id) {
        s32 count = 0;
        if (R_FAILED(::usbHsQueryAcquiredInterfaces(g_interfaces, sizeof(g_interfaces), std::addressof(count)))) {
            /* Unknown: keep the drive rather than tearing it down on a transient failure. */
            return true;
        }

        return std::any_of(g_interfaces, g_interfaces + count, [interface_id](const ::UsbHsInterface &intf) {
            return intf.inf.ID == interface_id;
        });
    }

    void WaitForEvent() {
        WaitForEvent(TimeSpan::FromNanoSeconds(std::numeric_limits<s64>::max()));
    }

    void WaitForEvent(TimeSpan timeout) {
        ::Event *state_change_event = ::usbHsGetInterfaceStateChangeEvent();

        const ::Waiter waiters[] = { ::waiterForEvent(std::addressof(g_interface_available_event)), ::waiterForEvent(state_change_event) };

        s32 signaled_index;
        R_DISCARD(::waitObjects(std::addressof(signaled_index), waiters, util::size(waiters), static_cast<u64>(std::max<s64>(timeout.GetNanoSeconds(), 0))));
        ::eventClear(state_change_event);
    }

}
