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
#include "../amsmitm_module.hpp"

namespace ams::mitm::usbl {

    DEFINE_MITM_MODULE_CLASS(0x8000, AMS_GET_SYSTEM_THREAD_PRIORITY(mitm_usbl, IpcServer));

    /* USB Loader NX is on and its service is up (fs_mitm only offers the USB drives to homebrew then). */
    bool IsStarted();

}
