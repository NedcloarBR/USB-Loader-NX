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
#include "usbl_config.hpp"
#include "usbl_log.hpp"

namespace ams::mitm::usbl {

    namespace {

        constexpr const char SettingsSection[] = "usbloadernx";

        struct ConfigKeyInfo {
            const char *name;
            bool default_enabled;
            const char *description;
        };

        /* Defaults and descriptions match config_templates/usbloadernx.ini. */
        constexpr ConfigKeyInfo ConfigKeys[] = {
            { "auto_register",     true,  "Register games found on USB in the SD card ncm database and in ns, so they show up on HOME." },
            { "notifications",     true,  "Ultrahand notifications when a drive is connected or removed."                                },
            { "badge",             true,  "USB + game card badge on the icons of games served from USB."                                 },
            { "hide_when_removed", false, "Hide games from HOME while their drive is unplugged."                                         },
        };
        static_assert(util::size(ConfigKeys) == static_cast<size_t>(ConfigKey::Count));

        constexpr size_t MaxConfigFileSize = 4_KB;

        constinit os::SdkMutex g_config_lock;
        constinit util::optional<bool> g_file_values[util::size(ConfigKeys)] = {};

        void FormatConfigPath(char *dst, size_t dst_size) {
            util::SNPrintf(dst, dst_size, "%s:/atmosphere/config/usbloadernx/config.ini", ams::fs::impl::SdCardFileSystemMountName);
        }

        /* Accepts the Manager's 0/1, true/false, and the u8!0x… form people copy from system_settings.ini. */
        util::optional<bool> ParseBool(const char *value) {
            if (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 || std::strcmp(value, "u8!0x1") == 0) {
                return true;
            }
            if (std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 || std::strcmp(value, "u8!0x0") == 0) {
                return false;
            }
            return util::nullopt;
        }

        int ConfigIniHandler(void *, const char *section, const char *name, const char *value) {
            if (std::strcmp(section, SettingsSection) != 0) {
                return 1;
            }

            for (size_t i = 0; i < util::size(ConfigKeys); ++i) {
                if (std::strcmp(name, ConfigKeys[i].name) != 0) {
                    continue;
                }
                if (const auto parsed = ParseBool(value); parsed.has_value()) {
                    g_file_values[i] = parsed;
                } else {
                    Log("config: %s = '%s' ignored (use 0 or 1)", name, value);
                }
                return 1;
            }

            const bool is_system_setting = std::strcmp(name, "enable") == 0 || std::strcmp(name, "debug") == 0;
            Log("config: unknown key '%s' ignored%s", name, is_system_setting ? " (set it in system_settings.ini)" : "");
            return 1;
        }

        /* Rewrites the whole file: created on first boot with the defaults, then by the Manager. Requires g_config_lock. */
        Result WriteConfigFile(const char *path, const bool *values) {
            char text[MaxConfigFileSize];
            size_t len = util::SNPrintf(text, sizeof(text), "[%s]\n; 0 = Disabled, 1 = Enabled\n", SettingsSection);
            for (size_t i = 0; i < util::size(ConfigKeys) && len < sizeof(text); ++i) {
                len += util::SNPrintf(text + len, sizeof(text) - len, "; %s\n%s = %d\n", ConfigKeys[i].description, ConfigKeys[i].name, values[i] ? 1 : 0);
            }
            R_UNLESS(len < sizeof(text), ams::fs::ResultInvalidSize());

            char directory[ams::fs::EntryNameLengthMax + 1];
            util::SNPrintf(directory, sizeof(directory), "%s:/atmosphere/config/usbloadernx", ams::fs::impl::SdCardFileSystemMountName);
            R_DISCARD(ams::fs::CreateDirectory(directory));

            R_DISCARD(ams::fs::DeleteFile(path));
            R_TRY(ams::fs::CreateFile(path, len));

            ams::fs::FileHandle file;
            R_TRY(ams::fs::OpenFile(std::addressof(file), path, ams::fs::OpenMode_Write));
            ON_SCOPE_EXIT { ams::fs::CloseFile(file); };
            R_RETURN(ams::fs::WriteFile(file, 0, text, len, ams::fs::WriteOption::Flush));
        }

        bool GetEffectiveValueLocked(size_t index) {
            return g_file_values[index].value_or(ConfigKeys[index].default_enabled);
        }

    }

    void LoadConfig() {
        std::scoped_lock lk(g_config_lock);

        for (auto &value : g_file_values) {
            value = util::nullopt;
        }

        char path[ams::fs::EntryNameLengthMax + 1];
        FormatConfigPath(path, sizeof(path));

        ams::fs::DirectoryEntryType type;
        if (R_FAILED(ams::fs::GetEntryType(std::addressof(type), path))) {
            bool defaults[util::size(ConfigKeys)];
            for (size_t i = 0; i < util::size(ConfigKeys); ++i) {
                defaults[i] = ConfigKeys[i].default_enabled;
            }
            if (const Result result = WriteConfigFile(path, defaults); R_FAILED(result)) {
                Log("config: config.ini not created 2%03d-%04d, using defaults", result.GetModule(), result.GetDescription());
                return;
            }
            Log("config: config.ini created with the defaults");
        }

        ams::fs::FileHandle file;
        if (R_FAILED(ams::fs::OpenFile(std::addressof(file), path, ams::fs::OpenMode_Read))) {
            Log("config: config.ini unreadable, using defaults");
            return;
        }
        ON_SCOPE_EXIT { ams::fs::CloseFile(file); };

        s64 size = 0;
        if (R_FAILED(ams::fs::GetFileSize(std::addressof(size), file)) || size > static_cast<s64>(MaxConfigFileSize)) {
            Log("config: config.ini unreadable or larger than %zu bytes, ignored", MaxConfigFileSize);
            return;
        }

        util::ini::ParseFile(file, nullptr, ConfigIniHandler);

        s32 count = 0;
        for (const auto &value : g_file_values) {
            count += value.has_value() ? 1 : 0;
        }
        Log("config: config.ini sets %d of %zu options (missing ones use the defaults)", count, util::size(ConfigKeys));
    }

    ConfigValue GetConfigValue(ConfigKey key) {
        const size_t index = static_cast<size_t>(key);
        AMS_ABORT_UNLESS(index < util::size(ConfigKeys));

        std::scoped_lock lk(g_config_lock);
        if (g_file_values[index].has_value()) {
            return { .enabled = *g_file_values[index], .is_default = false, .reserved = {} };
        }
        return { .enabled = ConfigKeys[index].default_enabled, .is_default = true, .reserved = {} };
    }

    Result SetConfigValue(ConfigKey key, bool enabled) {
        const size_t index = static_cast<size_t>(key);
        R_UNLESS(index < util::size(ConfigKeys), ams::fs::ResultInvalidArgument());

        std::scoped_lock lk(g_config_lock);

        bool values[util::size(ConfigKeys)];
        for (size_t i = 0; i < util::size(ConfigKeys); ++i) {
            values[i] = GetEffectiveValueLocked(i);
        }
        values[index] = enabled;

        char path[ams::fs::EntryNameLengthMax + 1];
        FormatConfigPath(path, sizeof(path));
        R_TRY(WriteConfigFile(path, values));

        /* Every key is now in the file. */
        for (size_t i = 0; i < util::size(ConfigKeys); ++i) {
            g_file_values[i] = values[i];
        }
        Log("config: %s = %d (set by the Manager)", ConfigKeys[index].name, enabled ? 1 : 0);
        R_SUCCEED();
    }

    bool IsConfigEnabled(ConfigKey key) {
        return GetConfigValue(key).enabled;
    }

}
