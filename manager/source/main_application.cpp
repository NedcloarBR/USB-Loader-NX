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
#include <main_application.hpp>
#include <art.hpp>
#include <algorithm>
#include <cinttypes>
#include <iterator>
#include <cstdio>
#include <game_library.hpp>
#include <manager_settings.hpp>
#include <migration.hpp>
#include <sound.hpp>
#include <usbl/version.hpp>

namespace ui {

    namespace {

        constexpr const char SortKey[] = "sort";

        constexpr strings::Id SortNames[] = { strings::Id::SortName, strings::Id::SortRecentlyPlayed, strings::Id::SortMostPlayed, strings::Id::SortSize };
        static_assert(std::size(SortNames) == static_cast<size_t>(library::SortMode::Count));

        /* Holding a direction repeats it, like on HOME: once after RepeatDelayNs, then every RepeatIntervalNs. */
        constexpr u64 Directions       = HidNpadButton_AnyLeft | HidNpadButton_AnyRight | HidNpadButton_AnyUp | HidNpadButton_AnyDown;
        constexpr u64 RepeatDelayNs    = 400'000'000;
        constexpr u64 RepeatIntervalNs = 90'000'000;

        class DirectionRepeat {
            private:
                u64 held = 0;
                u64 next_tick = 0;
            public:
                /* `keys_down` plus the held direction whenever it is due again. */
                u64 Apply(u64 keys_down, u64 keys_held) {
                    const u64 now = armGetSystemTick();
                    if (const u64 pressed = keys_down & Directions; pressed != 0) {
                        this->held = pressed;
                        this->next_tick = now + armNsToTicks(RepeatDelayNs);
                        return keys_down;
                    }
                    if (this->held == 0 || (keys_held & this->held) != this->held) {
                        this->held = 0;
                        return keys_down;
                    }
                    if (now < this->next_tick) {
                        return keys_down;
                    }
                    this->next_tick = now + armNsToTicks(RepeatIntervalNs);
                    return keys_down | this->held;
                }
        };

        DirectionRepeat g_direction_repeat;

        /* Drives and games come and go while the Manager is open; a few seconds is how long the module takes to index a drive. */
        constexpr u64 PollIntervalNs = 1'500'000'000;

        /* Everything the library screen shows that the module can change behind its back. */
        std::string TakeSnapshot() {
            std::vector<usbl::DriveStatus> drives;
            std::vector<usbl::GameStatus> games;
            if (R_FAILED(usbl::ListDrives(&drives)) || R_FAILED(usbl::ListGames(&games))) {
                return {};
            }
            std::string snapshot;
            for (const auto &drive : drives) {
                snapshot.append(reinterpret_cast<const char *>(&drive), sizeof(drive));
            }
            for (const auto &game : games) {
                snapshot.append(reinterpret_cast<const char *>(&game), sizeof(game));
            }
            return snapshot;
        }

        std::string FormatResult(Result rc) {
            char text[16];
            std::snprintf(text, sizeof(text), "2%03" PRIu32 "-%04" PRIu32, R_MODULE(rc), R_DESCRIPTION(rc));
            return text;
        }

    }

    void MainApplication::OnLoad() {
        migration::MoveLegacyFiles();
        settings::Load();
        strings::Initialize();
        ui::theme::InstallZips();
        ui::theme::ApplySaved();
        sound::Initialize();
        this->sort_mode = static_cast<library::SortMode>(std::clamp<s32>(settings::GetInt(SortKey, 0), 0, static_cast<s32>(library::SortMode::Count) - 1));

        this->library_screen = LibraryScreen::New(LibraryScreen::Actions {
            .launch        = [this](const library::Game &game) { this->Launch(game); },
            .toggle_hidden = [this](const library::Game &game) { this->ToggleHidden(game); },
            .open_settings = [this]() { this->ShowSettings(); },
            .exit          = [this]() { this->Quit(); },
            .toggle_favorite = [this](const library::Game &game) { this->ToggleFavorite(game); },
            .next_sort     = [this]() { this->NextSort(); },
            .next_filter   = [this]() { this->NextFilter(); },
            .show_drive    = [this](s32 drive) {
                std::vector<usbl::DriveStatus> drives;
                usbl::ListDrives(&drives);
                for (const auto &item : drives) {
                    if (item.drive == drive) {
                        this->filter = library::Filter::Drive;
                        this->filter_drive = drive;
                        this->filter_name = item.name;
                        this->ShowLibrary();
                        return;
                    }
                }
            },
            .search        = [this]() { this->Search(); },
            .clear_search  = [this]() {
                sound::Play(sound::Effect::Back);
                this->search.clear();
                this->ShowLibrary();
            },
        });
        this->settings_screen = SettingsScreen::New([this]() {
            this->ShowLibrary();
            this->library_screen->Enter();
        });

        this->system = system_check::Run();
        this->service_available = this->system.state == system_check::State::Ready;
        /* The language chosen in the settings (protocol 1.1), kept in config.ini with the module's. */
        if (std::string language; this->service_available && usbl::GetModuleMinorVersion() >= 1 && R_SUCCEEDED(usbl::GetLanguage(&language)) && !language.empty()) {
            strings::Initialize(language);
        }
        this->ShowLibrary();
        this->library_screen->Enter();
        this->ShowSystemNotice();

        this->AddRenderCallback([this]() { this->PollLibrary(); });
        this->SetOnInput([this](const u64 pressed, const u64, const u64 keys_held, const pu::ui::TouchPoint touch) {
            const u64 keys_down = g_direction_repeat.Apply(pressed, keys_held);
            if (this->screen == Screen::Library) {
                this->library_screen->OnTouch(touch);
                this->library_screen->OnInput(keys_down);
            } else {
                this->settings_screen->OnTouch(touch);
                this->settings_screen->OnInput(keys_down);
            }
        });
    }

    void MainApplication::ShowLibrary() {
        std::vector<library::Game> games;
        std::vector<usbl::DriveStatus> drives;
        std::string message;

        /* Taken before loading: a change that lands while the games load shows up at the next poll instead of being
         * folded into the snapshot and missed. */
        this->library_snapshot = this->service_available ? TakeSnapshot() : std::string();
        if (!this->service_available) {
            switch (this->system.state) {
                case system_check::State::NotOurAtmosphere: message = strings::Get(strings::Id::SystemNotOurAtmosphereTitle); break;
                case system_check::State::Disabled:         message = strings::Get(strings::Id::SystemDisabledTitle);         break;
                case system_check::State::NotStarted:       message = strings::Get(strings::Id::SystemNotStartedTitle);       break;
                case system_check::State::VersionMismatch:  message = strings::Get(strings::Id::SystemVersionMismatchTitle);  break;
                case system_check::State::Ready:            message = strings::Get(strings::Id::ServiceUnavailable);          break;
            }
        } else if (const Result rc = library::LoadGames(&games); R_FAILED(rc)) {
            message = strings::Format(strings::Id::ActionFailed, { { "CODE", FormatResult(rc) } });
        } else {
            usbl::ListDrives(&drives);
            std::vector<art::Game> art_games;
            for (const auto &game : games) {
                art_games.push_back({ game.status.application_id, game.name, game.names });
            }
            art::SetGames(std::move(art_games));
            library::SortGames(&games, this->sort_mode);
            this->library_screen->SetAllGames(games);
            library::FilterBy(&games, this->filter, this->filter_drive);
            library::FilterGames(&games, this->search);
            if (games.empty() && !this->search.empty()) {
                message = strings::Format(strings::Id::NoMatch, { { "QUERY", this->search } });
            } else if (games.empty() && this->filter != library::Filter::All) {
                message = strings::Format(strings::Id::NoGamesInFilter, { { "FILTER", this->filter_name } });
            }
        }

        this->library_drive_count = drives.size();
        this->next_poll_tick = armGetSystemTick() + armNsToTicks(PollIntervalNs);
        this->library_screen->Show(std::move(games), std::move(drives), std::move(message),
                                   { .sort_name = strings::Get(SortNames[static_cast<size_t>(this->sort_mode)]), .query = this->search,
                                     .filter_name = this->filter == library::Filter::All ? strings::Get(strings::Id::FilterAll) : this->filter_name });
        this->screen = Screen::Library;
        this->LoadLayout(this->library_screen);
    }

    void MainApplication::PollLibrary() {
        if (armGetSystemTick() < this->next_poll_tick) {
            return;
        }
        this->next_poll_tick = armGetSystemTick() + armNsToTicks(PollIntervalNs);
        /* A theme edited while the Manager is open shows at once. */
        if (ui::theme::ReloadIfChanged()) {
            sound::Reload();
            if (this->screen == Screen::Settings) {
                this->settings_screen->Refresh();
            } else {
                this->ShowLibrary();
            }
            return;
        }
        if (!this->service_available) {
            return;
        }
        if (this->screen == Screen::Settings) {
            this->settings_screen->Poll();
            return;
        }
        if (this->screen != Screen::Library) {
            return;
        }
        if (TakeSnapshot() != this->library_snapshot) {
            const size_t drive_count = this->library_drive_count;
            this->ShowLibrary();
            if (this->library_drive_count != drive_count) {
                sound::Play(this->library_drive_count > drive_count ? sound::Effect::Plug : sound::Effect::Unplug);
            }
        }
    }

    void MainApplication::ShowSettings() {
        if (!this->service_available) {
            return;
        }
        sound::Play(sound::Effect::Open);
        this->settings_screen->Show();
        this->screen = Screen::Settings;
        this->LoadLayout(this->settings_screen);
    }

    void MainApplication::ShowError(strings::Id id, Result rc) {
        sound::Play(sound::Effect::Error);
        this->library_screen->ShowMessage(strings::Get(strings::Id::AppTitle), strings::Format(id, { { "CODE", FormatResult(rc) } }), false);
    }

    void MainApplication::ShowSystemNotice() {
        using strings::Id;
        const auto &report = this->system;
        const std::initializer_list<strings::Argument> atmosphere = { { "VERSION", report.atmosphere_version }, { "COMMIT", report.atmosphere_commit } };

        switch (report.state) {
            case system_check::State::NotOurAtmosphere:
                this->library_screen->ShowMessage(strings::Get(Id::SystemNotOurAtmosphereTitle), strings::Format(Id::SystemNotOurAtmosphere, atmosphere), false);
                return;
            case system_check::State::NotStarted:
                this->library_screen->ShowMessage(strings::Get(Id::SystemNotStartedTitle), strings::Format(Id::SystemNotStarted, atmosphere), false);
                return;
            case system_check::State::Disabled:
                if (report.file_enables) {
                    this->library_screen->ShowMessage(strings::Get(Id::SystemDisabledTitle), strings::Format(Id::SystemDisabledRestart, { { "FILE", report.settings_file } }), false,
                                                      { strings::Get(Id::SystemRestart), [] { system_check::Reboot(); } });
                } else {
                    this->library_screen->ShowMessage(strings::Get(Id::SystemDisabledTitle), strings::Format(Id::SystemDisabled, { { "FILE", report.settings_file } }), false,
                                                      { strings::Get(Id::SystemEnableAndRestart), [this] { this->EnableAndRestart(); } });
                }
                return;
            case system_check::State::VersionMismatch: {
                /* No GetVersion: a module from before protocol 1.0. Otherwise the major versions say which side to update. */
                const std::string module(report.version.module_version, strnlen(report.version.module_version, sizeof(report.version.module_version)));
                const Id text = !report.has_version ? Id::SystemModuleTooOld : (report.version.major < usbl::protocol::VersionMajor ? Id::SystemModuleOlder : Id::SystemModuleNewer);
                this->library_screen->ShowMessage(strings::Get(Id::SystemVersionMismatchTitle), strings::Format(text, { { "MANAGER", usbl::Version }, { "MODULE", module } }), false);
                return;
            }
            case system_check::State::Ready:
                break;
        }

        /* Files a newer version wrote: read, not rewritten (FORMATS.md). */
        std::string text;
        const auto add = [&text](const std::string &paragraph) { text += (text.empty() ? "" : " ") + paragraph; };
        const bool newer_config = (report.version.flags & usbl::protocol::VersionFlag_ConfigFromNewerVersion) != 0;
        if (newer_config) {
            add(strings::Get(Id::SystemNewerConfig));
        }
        if ((report.version.flags & usbl::protocol::VersionFlag_DataFromNewerVersion) != 0 || library::AreFavoritesFromNewerVersion()) {
            add(strings::Get(Id::SystemNewerData));
        }
        if (settings::IsFromNewerVersion()) {
            add(strings::Get(Id::SystemNewerManager));
        }
        if (text.empty()) {
            return;
        }
        LibraryScreen::MessageAction write_anyway;
        if (newer_config) {
            write_anyway = { strings::Get(Id::SystemWriteAnyway), [] {
                sound::Play(sound::Effect::ToggleOn);
                usbl::SetOverwriteNewerConfig(true);
            } };
        }
        this->library_screen->ShowMessage(strings::Get(Id::SystemNewerConfigTitle), text, false, std::move(write_anyway));
    }

    void MainApplication::EnableAndRestart() {
        if (const Result rc = system_check::EnableUsbLoaderNx(this->system.settings_file); R_FAILED(rc)) {
            sound::Play(sound::Effect::Error);
            this->library_screen->ShowMessage(strings::Get(strings::Id::SystemDisabledTitle),
                                              strings::Format(strings::Id::SystemEnableFailed, { { "FILE", this->system.settings_file }, { "CODE", FormatResult(rc) } }), false);
            return;
        }
        sound::Play(sound::Effect::Launch);
        this->FadeOut();
        sound::Exit();
        system_check::Reboot();
    }

    void MainApplication::Launch(const library::Game &game) {
        if (library::IsUpdateMissing(game.status.application_id)) {
            sound::Play(sound::Effect::Error);
            this->library_screen->ShowMessage(game.name, strings::Get(strings::Id::UpdateRequired), true);
            return;
        }
        sound::Play(sound::Effect::Launch);
        this->FadeOut();
        if (const Result rc = library::LaunchGame(game); R_FAILED(rc)) {
            this->FadeIn();
            this->ShowError(strings::Id::LaunchFailed, rc);
            return;
        }
        sound::Exit();
        this->Close(true);
    }

    void MainApplication::Quit() {
        sound::Play(sound::Effect::Back);
        this->FadeOut();
        sound::Exit();
        this->Close();
    }

    void MainApplication::ToggleFavorite(const library::Game &game) {
        const bool favorite = !game.is_favorite;
        if (!library::SetFavorite(game.status.application_id, favorite)) {
            sound::Play(sound::Effect::Error);
            return;
        }
        sound::Play(favorite ? sound::Effect::ToggleOn : sound::Effect::ToggleOff);
        this->ShowLibrary();
    }

    void MainApplication::NextSort() {
        this->sort_mode = static_cast<library::SortMode>((static_cast<s32>(this->sort_mode) + 1) % static_cast<s32>(library::SortMode::Count));
        settings::SetInt(SortKey, static_cast<s32>(this->sort_mode));
        sound::Play(sound::Effect::Whoosh);
        this->ShowLibrary();
    }

    /* All, continue playing, favorites, each plugged-in drive, then by format and the hidden ones; back to all. */
    void MainApplication::NextFilter() {
        struct Choice {
            library::Filter filter;
            s32 drive;
            std::string name;
        };
        std::vector<Choice> choices = {
            { library::Filter::All, 0, strings::Get(strings::Id::FilterAll) },
            { library::Filter::ContinuePlaying, 0, strings::Get(strings::Id::FilterContinue) },
            { library::Filter::Favorites, 0, strings::Get(strings::Id::FilterFavorites) },
        };
        std::vector<usbl::DriveStatus> drives;
        if (this->service_available && R_SUCCEEDED(usbl::ListDrives(&drives)) && drives.size() > 1) {
            for (const auto &drive : drives) {
                choices.push_back({ library::Filter::Drive, drive.drive, drive.name });
            }
        }
        choices.push_back({ library::Filter::Xci, 0, "XCI / XCZ" });
        choices.push_back({ library::Filter::Nsp, 0, "NSP / NSZ" });
        choices.push_back({ library::Filter::Installed, 0, strings::Get(strings::Id::FilterInstalled) });
        choices.push_back({ library::Filter::Hidden, 0, strings::Get(strings::Id::FilterHidden) });

        size_t at = 0;
        for (size_t i = 0; i < choices.size(); ++i) {
            if (choices[i].filter == this->filter && (this->filter != library::Filter::Drive || choices[i].drive == this->filter_drive)) {
                at = i;
                break;
            }
        }
        const Choice &next = choices[(at + 1) % choices.size()];
        this->filter = next.filter;
        this->filter_drive = next.drive;
        this->filter_name = next.name;
        sound::Play(sound::Effect::Whoosh);
        this->ShowLibrary();
    }

    /* The console's keyboard; cancelling it keeps the current search. */
    void MainApplication::Search() {
        SwkbdConfig keyboard;
        if (R_FAILED(swkbdCreate(&keyboard, 0))) {
            sound::Play(sound::Effect::Error);
            return;
        }
        sound::Play(sound::Effect::Open);
        swkbdConfigMakePresetDefault(&keyboard);
        swkbdConfigSetHeaderText(&keyboard, strings::Get(strings::Id::SearchHeader).c_str());
        swkbdConfigSetInitialText(&keyboard, this->search.c_str());
        swkbdConfigSetStringLenMax(&keyboard, 32);
        char text[0x80] = {};
        const Result rc = swkbdShow(&keyboard, text, sizeof(text));
        swkbdClose(&keyboard);
        if (R_SUCCEEDED(rc)) {
            this->search = text;
            this->ShowLibrary();
        }
    }

    void MainApplication::ToggleHidden(const library::Game &game) {
        /* `game` lives in the library screen, which ShowLibrary replaces. */
        const u64 application_id = game.status.application_id;
        const bool hidden = !game.status.is_hidden;
        if (const Result rc = usbl::SetGameHidden(application_id, hidden); R_FAILED(rc)) {
            this->ShowError(strings::Id::ActionFailed, rc);
        } else {
            sound::Play(hidden ? sound::Effect::ToggleOff : sound::Effect::ToggleOn);
        }
        this->ShowLibrary();
    }

}
