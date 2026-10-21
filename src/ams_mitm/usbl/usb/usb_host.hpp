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

namespace ams::mitm::usbl::usb {

    Result Initialize();

    /* Mass storage interfaces plugged in and not yet acquired by anyone. */
    s32 QueryAvailableInterfaces(::UsbHsInterface *out, s32 max_count);

    /* Whether an interface this process acquired is still attached. */
    bool IsInterfaceAcquired(s32 interface_id);

    /* Blocks until a drive is plugged in or an acquired one changes state (usually: is unplugged). */
    void WaitForEvent();
    /* Returns early when the timeout elapses. */
    void WaitForEvent(TimeSpan timeout);

}
