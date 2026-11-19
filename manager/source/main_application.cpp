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
#include <algorithm>
#include <cinttypes>
#include <iterator>
#include <cstdio>
#include <manager_settings.hpp>
#include <sound.hpp>

namespace ui {

    namespace {

        constexpr const char SortKey[] = "sort";

        constexpr strings::Id SortNames[] = { strings::Id::SortName, strings::Id::SortRecentlyPlayed, strings::Id::SortMostPlayed, strings::Id::SortSize };
        static_assert(std::size(SortNames) == static_cast<size_t>(library::SortMode::Count));

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
        settings::Load();
        strings::Initialize();
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

        this->service_available = R_SUCCEEDED(usbl::Initialize());
        this->ShowLibrary();
        this->library_screen->Enter();

        this->AddRenderCallback([this]() { this->PollLibrary(); });
        this->SetOnInput([this](const u64 keys_down, const u64, const u64, const pu::ui::TouchPoint) {
            if (this->screen == Screen::Library) {
                this->library_screen->OnInput(keys_down);
            } else {
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
            message = strings::Get(strings::Id::ServiceUnavailable);
        } else if (const Result rc = library::LoadGames(&games); R_FAILED(rc)) {
            message = strings::Format(strings::Id::ActionFailed, { { "CODE", FormatResult(rc) } });
        } else {
            usbl::ListDrives(&drives);
            library::SortGames(&games, this->sort_mode);
            library::FilterGames(&games, this->search);
            if (games.empty() && !this->search.empty()) {
                message = strings::Format(strings::Id::NoMatch, { { "QUERY", this->search } });
            }
        }

        this->library_drive_count = drives.size();
        this->next_poll_tick = armGetSystemTick() + armNsToTicks(PollIntervalNs);
        this->library_screen->Show(std::move(games), std::move(drives), std::move(message),
                                   { .sort_name = strings::Get(SortNames[static_cast<size_t>(this->sort_mode)]), .query = this->search });
        this->screen = Screen::Library;
        this->LoadLayout(this->library_screen);
    }

    void MainApplication::PollLibrary() {
        if (!this->service_available || this->screen != Screen::Library || armGetSystemTick() < this->next_poll_tick) {
            return;
        }
        this->next_poll_tick = armGetSystemTick() + armNsToTicks(PollIntervalNs);
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
