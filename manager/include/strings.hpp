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
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <switch.h>

/* UI text from one JSON file per language built into the app (romfs:/i18n/<locale>.json): en-US first, then the
 * console's language. A key missing in a language keeps the English text. */
namespace strings {

    /* Each id is a "Section.Key" of the JSON files (see strings.cpp). */
    enum class Id {
        AppTitle,
        ServiceUnavailable,
        NoGames,
        NoDrives,
        FreeOf,
        GameOne, GameMany,
        UpdateOne, UpdateMany,
        AddOnOne, AddOnMany,
        DriveOne, DriveMany,
        OnDrive,
        Disconnected,
        Hidden,
        PlugInDrive,
        SourceXci,
        SourceNsp,
        SourceInstalled,
        Version,
        Contiguous,
        FragmentedParts,
        HeavilyFragmented,
        PartsElsewhere,
        Launch,
        HideFromHome,
        ShowOnHome,
        Back,
        Ok,
        DevicesTitle,
        DriveNoGamesFolder,
        DriveUnreadable,
        DriveReading,
        CountingSpace,
        DriveNoGames,
        DriveHomebrewPath,
        AlmostFull,
        Total,
        SettingsTitle,
        SectionHome,
        SectionManager,
        SectionAbout,
        SettingAutoRegister, SettingAutoRegisterHelp,
        SettingHideWhenRemoved, SettingHideWhenRemovedHelp,
        SettingBadge, SettingBadgeHelp,
        SettingNotifications, SettingNotificationsHelp,
        SettingHomebrewAccess, SettingHomebrewAccessHelp,
        SettingNeedsNewerModule,
        SettingNeedsAutoRegister,
        SettingSounds, SettingSoundsHelp,
        HintToggle,
        HintSelect,
        HintBack,
        AboutVersion,
        AboutSummary,
        AboutLicense,
        LaunchFailed,
        UpdateRequired,
        ActionFailed,
        PlayTimeHours,
        PlayTimeMinutes,
        NeverPlayed,
        LastPlayed,
        SortName,
        SortRecentlyPlayed,
        SortMostPlayed,
        SortSize,
        Search,
        SearchHeader,
        NoMatch,
        DateFormat,
        SettingTheme,
        SettingThemeHelp,
        SettingThemeDefault,
        SettingMusic,
        SettingMusicHelp,
        SettingMusicUnavailable,
        GameOf,
        DrivesFree,
        RowVersion,
        RowUpdate,
        RowAddOns,
        RowSize,
        RowFile,
        RowPlayTime,
        RowLastPlayed,
        UpdateNone,
        UpdateInstalled,
        HintDrives,
        HintSettings,
        HintGrid,
        HintCarousel,
        HintExit,
        HintFavorite,
        HintUnfavorite,
        HintSwitchGame,
        HintClearSearch,
        SectionHomeHelp,
        SectionManagerHelp,
        UpdateMissing,
        UpdateMissingHelp,
        UpdateMissingRow,
        UpdateMissingBadge,
        Details,
        ThemeInUse,
        HintApply,
        SettingMusicVolume,
        SettingMusicVolumeHelp,
        UpdateElsewhereRow,
        PartsElsewhereBadge,
        PartsElsewhereHelp,
        SystemNotOurAtmosphereTitle,
        SystemNotOurAtmosphere,
        SystemDisabledTitle,
        SystemDisabled,
        SystemDisabledRestart,
        SystemEnableAndRestart,
        SystemRestart,
        SystemEnableFailed,
        SystemNotStartedTitle,
        SystemNotStarted,
        SystemVersionMismatchTitle,
        SystemModuleTooOld,
        SystemModuleOlder,
        SystemModuleNewer,
        SystemNewerConfigTitle,
        SystemNewerConfig,
        SystemNewerData,
        SystemNewerManager,
        SystemWriteAnyway,
        Close,
        ThemeFromNewerVersion,

        SettingLanguage,
        SettingLanguageHelp,
        SettingLanguageSystem,
        DriveLeftOutMore,
        DriveLeftOutPathTooLong,
        DriveLeftOutTooDeep,
        DriveLeftOutTooManyEntries,
        DriveLeftOutIndexFull,
        DriveLeftOutSolid,
        SectionDiagnostics,
        SectionDiagnosticsHelp,
        DiagnosticsNone,
        DiagnosticsNeedsNewerModule,
        Count,
    };

    using Argument = std::pair<std::string_view, std::string>;

    /* Loads the strings of a locale ("pt-BR"), or of the console's language when it is empty. */
    void Initialize(const std::string &locale = {});
    const std::string &Get(Id id);

    /* Replaces each {{NAME}} with its argument. */
    std::string Format(Id id, std::initializer_list<Argument> args);

    /* "1 jogo", "3 jogos": the text's {{COUNT}}. */
    std::string Count(u64 count, Id one, Id many);

    /* "52,3 GB", with the language's decimal separator; "640 MB" under 1 GB. */
    std::string Size(u64 bytes);

}
