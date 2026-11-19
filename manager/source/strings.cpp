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
#include <strings.hpp>
#include <array>
#include <cstdio>
#include <cstring>
#include <json.hpp>

namespace strings {

    namespace {

        constexpr const char *Keys[] = {
            "App.Title",
            "App.ServiceUnavailable",
            "Library.NoGames",
            "Library.NoDrives",
            "Library.FreeOf",
            "Count.GameOne", "Count.GameMany",
            "Count.UpdateOne", "Count.UpdateMany",
            "Count.AddOnOne", "Count.AddOnMany",
            "Count.DriveOne", "Count.DriveMany",
            "Game.OnDrive",
            "Game.Disconnected",
            "Game.Hidden",
            "Game.PlugInDrive",
            "Game.SourceXci",
            "Game.SourceNsp",
            "Game.SourceInstalled",
            "Game.Version",
            "Game.Contiguous",
            "Game.FragmentedParts",
            "Game.HeavilyFragmented",
            "Game.PartsElsewhere",
            "Game.Launch",
            "Game.HideFromHome",
            "Game.ShowOnHome",
            "Common.Back",
            "Common.Ok",
            "Drives.Title",
            "Drives.NoGamesFolder",
            "Drives.Unreadable",
            "Drives.AlmostFull",
            "Drives.Total",
            "Settings.Title",
            "Settings.SectionHome",
            "Settings.SectionManager",
            "Settings.SectionAbout",
            "Settings.AutoRegister", "Settings.AutoRegisterHelp",
            "Settings.HideWhenRemoved", "Settings.HideWhenRemovedHelp",
            "Settings.Badge", "Settings.BadgeHelp",
            "Settings.Notifications", "Settings.NotificationsHelp",
            "Settings.NeedsAutoRegister",
            "Settings.Sounds", "Settings.SoundsHelp",
            "Hints.Toggle",
            "Hints.Select",
            "Hints.Back",
            "About.Version",
            "About.Summary",
            "About.License",
            "Errors.LaunchFailed",
            "Errors.UpdateRequired",
            "Errors.ActionFailed",
            "Game.PlayTimeHours",
            "Game.PlayTimeMinutes",
            "Game.NeverPlayed",
            "Game.LastPlayed",
            "Sort.Name",
            "Sort.RecentlyPlayed",
            "Sort.MostPlayed",
            "Sort.Size",
            "Library.Search",
            "Library.SearchHeader",
            "Library.NoMatch",
            "Format.Date",
            "Settings.Theme",
            "Settings.ThemeHelp",
            "Settings.ThemeDefault",
            "Settings.Music",
            "Settings.MusicHelp",
            "Settings.MusicUnavailable",
            "Library.GameOf",
            "Library.DrivesFree",
            "Game.RowVersion",
            "Game.RowUpdate",
            "Game.RowAddOns",
            "Game.RowSize",
            "Game.RowFile",
            "Game.RowPlayTime",
            "Game.RowLastPlayed",
            "Game.UpdateNone",
            "Game.UpdateInstalled",
            "Hints.Drives",
            "Hints.Settings",
            "Hints.Grid",
            "Hints.Carousel",
            "Hints.Exit",
            "Hints.Favorite",
            "Hints.Unfavorite",
            "Hints.SwitchGame",
            "Hints.ClearSearch",
            "Settings.SectionHomeHelp",
            "Settings.SectionManagerHelp",
            "Game.UpdateMissing",
            "Game.UpdateMissingHelp",
            "Game.UpdateMissingRow",
            "Game.UpdateMissingBadge",
            "Game.Details",
            "Settings.ThemeInUse",
            "Hints.Apply",
            "Settings.MusicVolume",
            "Settings.MusicVolumeHelp",
            "Game.UpdateElsewhereRow",
            "Game.PartsElsewhereBadge",
            "Game.PartsElsewhereHelp",
        };
        static_assert(std::size(Keys) == static_cast<size_t>(Id::Count));

        constexpr const char DefaultLocale[] = "en-US";
        constexpr const char DecimalSeparatorKey[] = "Format.DecimalSeparator";

        /* set:sys gives the language as its code ("pt-BR", "en-US", "ja"...). */
        std::string GetConsoleLocale() {
            u64 language_code = 0;
            if (R_FAILED(setInitialize())) {
                return DefaultLocale;
            }
            const bool ok = R_SUCCEEDED(setGetSystemLanguage(&language_code));
            setExit();
            if (!ok) {
                return DefaultLocale;
            }
            char code[sizeof(language_code) + 1] = {};
            std::memcpy(code, &language_code, sizeof(language_code));
            return code;
        }

        std::array<std::string, static_cast<size_t>(Id::Count)> g_strings;
        std::string g_decimal_separator = ".";

    }

    void Initialize() {
        /* Each file is merged whole or not at all, so a broken translation cannot leave half its keys. */
        json::Table table;
        json::ReadFlatFile(&table, std::string("romfs:/i18n/") + DefaultLocale + ".json");

        /* The console's language, else its language family ("pt" falls back to pt-BR, the one we ship). */
        const std::string locale = GetConsoleLocale();
        if (locale != DefaultLocale && !json::ReadFlatFile(&table, "romfs:/i18n/" + locale + ".json") && locale.rfind("pt", 0) == 0) {
            json::ReadFlatFile(&table, "romfs:/i18n/pt-BR.json");
        }

        for (size_t i = 0; i < g_strings.size(); ++i) {
            const auto found = table.find(Keys[i]);
            g_strings[i] = found != table.end() ? found->second : Keys[i];
        }
        if (const auto found = table.find(DecimalSeparatorKey); found != table.end()) {
            g_decimal_separator = found->second;
        }
    }

    const std::string &Get(Id id) {
        return g_strings[static_cast<size_t>(id)];
    }

    std::string Format(Id id, std::initializer_list<Argument> args) {
        std::string text = Get(id);
        for (const auto &[name, value] : args) {
            const std::string placeholder = "{{" + std::string(name) + "}}";
            for (size_t pos = text.find(placeholder); pos != std::string::npos; pos = text.find(placeholder, pos + value.size())) {
                text.replace(pos, placeholder.size(), value);
            }
        }
        return text;
    }

    std::string Count(u64 count, Id one, Id many) {
        return Format(count == 1 ? one : many, { { "COUNT", std::to_string(count) } });
    }

    std::string Size(u64 bytes) {
        constexpr double MiB = 1024.0 * 1024.0, GiB = 1024.0 * MiB;
        char text[32];
        if (bytes < static_cast<u64>(GiB)) {
            std::snprintf(text, sizeof(text), "%.0f MB", static_cast<double>(bytes) / MiB);
        } else {
            std::snprintf(text, sizeof(text), "%.1f GB", static_cast<double>(bytes) / GiB);
        }
        std::string size = text;
        if (const auto dot = size.find('.'); dot != std::string::npos) {
            size.replace(dot, 1, g_decimal_separator);
        }
        return size;
    }

}
