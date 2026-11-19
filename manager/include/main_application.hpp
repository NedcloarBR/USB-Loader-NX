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
#include <strings.hpp>
#include <ui/library_screen.hpp>
#include <ui/settings_screen.hpp>

namespace ui {

    class MainApplication : public pu::ui::Application {
        private:
            enum class Screen {
                Library,
                Settings,
            };

            Screen screen = Screen::Library;
            LibraryScreen::Ref library_screen;
            SettingsScreen::Ref settings_screen;
            bool service_available = false;
            /* What the drives and games looked like at the last refresh, and when to look again. */
            std::string library_snapshot;
            size_t library_drive_count = 0;
            u64 next_poll_tick = 0;
            /* Browsing the library: sort order (kept in manager.ini) and the search typed with ZL. */
            library::SortMode sort_mode = library::SortMode::Name;
            std::string search;

            void ShowLibrary();
            void ShowSettings();
            void ShowError(strings::Id id, Result rc);
            void PollLibrary();

            void Launch(const library::Game &game);
            void ToggleHidden(const library::Game &game);
            void ToggleFavorite(const library::Game &game);
            void NextSort();
            void Search();
            void Quit();

        public:
            using Application::Application;
            PU_SMART_CTOR(MainApplication)

            void OnLoad() override;
    };

}
