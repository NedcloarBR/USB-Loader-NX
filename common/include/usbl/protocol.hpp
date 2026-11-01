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
#include <cstddef>
#include <cstdint>
#include <type_traits>

/* Wire format of the "usbl" service that USB Loader NX registers inside ams.mitm.
 * Shared by the module (atmosphere/, built from src/) and its clients (manager/), so both always agree. */
namespace usbl::protocol {

    constexpr char ServiceName[] = "usbl";

    enum class CommandId : std::uint32_t {
        IsServedProgram      = 0,  /* loader */
        GetContentInfoForNcm = 4,  /* ncm */
        ReadNcaContent       = 5,  /* ncm */
        OpenCodeFileSystem   = 6,  /* loader */
        ForgetContentForNcm  = 8,  /* ncm */
        ReloadConfig         = 9,
        GetConfigValue       = 10,
        SetConfigValue       = 11,
        ListDrives           = 12,
        ListGames            = 13,
        SetGameHidden        = 14,
        /* Registers and shows one plugged-in game whatever auto_register and the Manager's hide say, so ns can launch it. */
        ShowGameForLaunch    = 15,
        /* Puts HOME back in line with the options (after ShowGameForLaunch, once the game was played). */
        ApplyHomeVisibility  = 16,
        /* The game's own icon JPEG, without the badge HOME shows; only while its drive is plugged in. */
        GetGameIcon          = 17,
        /* The game's version as players know it ("1.0.2", from the update when one is on the drive); only while its drive is plugged in. */
        GetGameDisplayVersion = 18,
    };

    struct DisplayVersion {
        char text[0x10];
    };
    static_assert(sizeof(DisplayVersion) == 0x10);

    /* NACP icons are 256x256 JPEGs, well under this. */
    constexpr std::size_t MaxIconSize = 0x40000;

    enum class ConfigKey : std::uint8_t {
        AutoRegister    = 0,
        Notifications   = 1,
        Badge           = 2,
        HideWhenRemoved = 3,

        Count,
    };

    struct ConfigValue {
        bool enabled;
        /* The key is missing from config.ini (or invalid there): the default applies. */
        bool is_default;
        std::uint8_t reserved[6];
    };
    static_assert(sizeof(ConfigValue) == 0x8);

    enum class DriveState : std::uint8_t {
        Unreadable    = 0,
        NoGamesFolder = 1,
        Ready         = 2,
    };

    struct DriveStatus {
        std::int32_t drive;
        DriveState state;
        bool is_exfat;
        std::uint8_t reserved[2];
        std::uint64_t capacity;
        /* Free space of the mounted volume; 0 when it could not be counted. */
        std::uint64_t free_space;
        std::uint32_t game_count;
        std::uint32_t update_count;
        std::uint32_t add_on_count;
        std::uint32_t reserved2;
        char name[0x30];
    };
    static_assert(sizeof(DriveStatus) == 0x58);

    /* Where a game's content comes from; also picks the icon badge. */
    enum class GameSource : std::uint8_t {
        Xci       = 0,
        Nsp       = 1,
        Installed = 2,
    };

    struct GameStatus {
        std::uint64_t application_id;
        /* Registered versions (SD card ncm database); 0 when not registered. */
        std::uint32_t version;
        std::uint32_t update_version;
        /* Game, update and DLC on the drive, in bytes. */
        std::uint64_t size;
        std::int32_t drive;
        /* Pieces of the game's file(s) on the drive: 1 = contiguous, negative = too fragmented to map; valid only when connected. */
        std::int32_t fragments;
        GameSource source;
        std::uint8_t add_on_count;
        bool has_update;
        bool is_connected;
        bool is_hidden;
        std::uint8_t reserved[3];
        char file_name[0x100];
    };
    static_assert(sizeof(GameStatus) == 0x128);

    /* Commands with several inputs take a single struct: libstratosphere lays separate arguments out sorted by alignment
     * (smallest first), which a client cannot see from the signature. */
    struct SetConfigValueInput {
        ConfigKey key;
        bool enabled;
    };
    static_assert(sizeof(SetConfigValueInput) == 0x2);

    struct SetGameHiddenInput {
        std::uint64_t application_id;
        bool hidden;
        std::uint8_t padding[7];
    };
    static_assert(sizeof(SetGameHiddenInput) == 0x10);

    static_assert(std::is_trivially_copyable_v<ConfigValue> && std::is_trivially_copyable_v<DriveStatus> && std::is_trivially_copyable_v<GameStatus>);

}
