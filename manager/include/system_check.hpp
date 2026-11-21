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
#include <string>
#include <switch.h>
#include <usbl_client.hpp>

/* What the Manager checks before showing the library: that the console runs the USB Loader NX build of Atmosphère, that
 * the module is on and running, and that it speaks the same protocol (FORMATS.md). */
namespace system_check {

    enum class State {
        /* The usbl service answered with the same protocol major version. */
        Ready,
        /* Another Atmosphère (or no Atmosphère): [usbloadernx] enable is not a setting it knows. */
        NotOurAtmosphere,
        /* [usbloadernx] enable = 0 in the settings Atmosphère read at boot. */
        Disabled,
        /* enable = 1, but the module did not register its service (see its log). */
        NotStarted,
        /* The module is older than protocol 1.0, or of another major version: the Manager writes nothing. */
        VersionMismatch,
    };

    struct Report {
        State state = State::NotOurAtmosphere;
        /* "1.12.2" and the first 9 digits of its commit, or "?" without Atmosphère. */
        std::string atmosphere_version = "?";
        std::string atmosphere_commit = "?";
        /* Disabled: the file to change for the boot option in use ("atmosphere/config/cfw_system_settings.ini"), and
         * whether it already turns USB Loader NX on (a restart is all that is missing). */
        std::string settings_file;
        bool file_enables = false;
        /* Ready and VersionMismatch: what the module reported (has_version false for a module before protocol 1.0). */
        bool has_version = false;
        usbl::VersionInfo version = {};
    };

    /* Connects to the usbl service when it can; on anything but Ready the connection is closed again. */
    Report Run();

    /* Writes "enable = u8!0x1" under [usbloadernx] in `settings_file` (and in system_settings.ini when that is another
     * file), keeping a copy of each as "<name>.bak". Writes to the SD card. */
    Result EnableUsbLoaderNx(const std::string &settings_file);

    void Reboot();

}
