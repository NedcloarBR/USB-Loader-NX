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

    /* The Manager's side of the service (FORMATS.md). A new command, or a new field in reserved bytes, raises the minor
     * version; changing what an existing command takes or returns raises the major one, and a Manager refuses to write
     * through a module of another major version. */
    constexpr std::uint16_t VersionMajor = 1;
    /* 1.1: ConfigKey::HomebrewAccess, DriveStatus::volume_serial, DriveState::Reading, GetLanguage and SetLanguage. */
    constexpr std::uint16_t VersionMinor = 1;

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
        /* The Manager is closing: games taken off HOME by a badge change come back now, so HOME fetches their new icons. */
        FinishHomeRefresh    = 19,
        GetVersion           = 20,
        /* 1.1: the language of the notifications and of the Manager (config.ini `language`). */
        GetLanguage          = 21,
        SetLanguage          = 22,
    };

    enum VersionFlag : std::uint32_t {
        /* config.ini was written by a newer version: read only until SetConfigValue says to overwrite it. */
        VersionFlag_ConfigFromNewerVersion = 1u << 0,
        /* A file in usbloadernx/data was written by a newer version and is left untouched. */
        VersionFlag_DataFromNewerVersion   = 1u << 1,
    };

    struct VersionInfo {
        std::uint16_t major;
        std::uint16_t minor;
        std::uint32_t flags;
        char module_version[0x18];
    };
    static_assert(sizeof(VersionInfo) == 0x20);

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
        /* 1.1 */
        HomebrewAccess  = 4,

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
        /* 1.1: listed while it is mounted and indexed; a 1.0 module lists a drive only once it is read. */
        Reading       = 3,
    };

    struct DriveStatus {
        std::int32_t drive;
        DriveState state;
        bool is_exfat;
        /* 1.1: free_space is still being counted (after the games are indexed). */
        bool is_counting_free_space;
        std::uint8_t reserved[1];
        /* Size of the mounted volume (the whole drive until it is mounted). */
        std::uint64_t capacity;
        /* Free space of the mounted volume; 0 when it could not be counted. */
        std::uint64_t free_space;
        std::uint32_t game_count;
        std::uint32_t update_count;
        std::uint32_t add_on_count;
        /* 1.1: the volume serial number; homebrew read the drive at sdmc:/usbloadernx/usb/XXXX-XXXX/ (0 from a 1.0 module). */
        std::uint32_t volume_serial;
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
        /* An update or add-on content of the game is on another drive: only the game's own drive is used. */
        bool has_parts_elsewhere;
        std::uint8_t reserved[2];
        char file_name[0x100];
    };
    static_assert(sizeof(GameStatus) == 0x128);

    /* Commands with several inputs take a single struct: libstratosphere lays separate arguments out sorted by alignment
     * (smallest first), which a client cannot see from the signature. */
    struct SetConfigValueInput {
        ConfigKey key;
        bool enabled;
        /* Also when config.ini comes from a newer version (VersionFlag_ConfigFromNewerVersion). */
        bool overwrite;
        std::uint8_t reserved[5];
    };
    static_assert(sizeof(SetConfigValueInput) == 0x8);

    /* A locale ("en-US", "pt-BR"), or empty for the console's language. */
    struct Language {
        char locale[0x10];
    };
    static_assert(sizeof(Language) == 0x10);

    struct SetLanguageInput {
        Language language;
        /* As in SetConfigValueInput. */
        bool overwrite;
        std::uint8_t reserved[7];
    };
    static_assert(sizeof(SetLanguageInput) == 0x18);

    struct SetGameHiddenInput {
        std::uint64_t application_id;
        bool hidden;
        std::uint8_t padding[7];
    };
    static_assert(sizeof(SetGameHiddenInput) == 0x10);

    static_assert(std::is_trivially_copyable_v<ConfigValue> && std::is_trivially_copyable_v<VersionInfo> && std::is_trivially_copyable_v<DriveStatus> && std::is_trivially_copyable_v<GameStatus>);

}
