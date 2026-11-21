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
#include <functional>
#include <vector>
#include <game_library.hpp>
#include <ui/motion.hpp>
#include <ui/widgets.hpp>

namespace ui {

    /* The home screen: a header with the sort order, the search and the drives; the games as a shelf (the selected one
     * large, the next ones queued after it) or a grid; a footer with the button hints; the game and drive windows over it. */
    class LibraryScreen : public pu::ui::Layout {
        public:
            enum class View {
                Carousel,
                Grid,
            };

            struct Actions {
                std::function<void(const library::Game &)> launch;
                std::function<void(const library::Game &)> toggle_hidden;
                std::function<void()> open_settings;
                std::function<void()> exit;
                std::function<void(const library::Game &)> toggle_favorite;
                std::function<void()> next_sort;
                std::function<void()> search;
                std::function<void()> clear_search;
            };

            /* A second button in a message window, next to Close: left and right pick one, A presses it, B closes. */
            struct MessageAction {
                std::string label;
                std::function<void()> run;
            };

            /* How the list is browsed, shown on the toolbar: the sort order's name and the search, if any. */
            struct Browse {
                std::string sort_name;
                std::string query;
            };

        private:
            enum class Window {
                None,
                Game,
                Drives,
                Message,
            };

            /* On the shelf view: the covers, or the Launch and Details buttons under them. */
            enum class Focus {
                Shelf,
                Buttons,
            };

            enum class HomeButton {
                Launch,
                Details,
            };

            /* Game window buttons, left to right. */
            enum class GameButton {
                ToggleHidden,
                Launch,
            };

            /* A window's text or image, moved with the window while it slides in. */
            struct SlidingElement {
                std::function<void(s32)> set_y;
                s32 base_y;
            };

            Actions actions;
            std::vector<library::Game> games;
            /* The name drawn on the cover of each game without an icon; null for the others. */
            std::vector<pu::sdl2::TextureHandle::Ref> name_textures;
            std::vector<usbl::DriveStatus> drives;
            pu::sdl2::TextureHandle::Ref app_icon;
            std::string message;
            Browse browse;

            View view = View::Carousel;
            Window window = Window::None;
            GameButton game_button = GameButton::Launch;
            Focus focus = Focus::Shelf;
            /* A message over the screen (an error, why a game cannot start); closing it goes back to `message_return`. */
            std::string message_title, message_text;
            bool message_has_icon = false;
            MessageAction message_action;
            /* With an action: which of the two buttons A presses (left/right move between them). */
            bool message_on_action = true;
            Window message_return = Window::None;
            HomeButton home_button = HomeButton::Launch;
            s32 selected = 0;
            s32 first_row = 0;

            /* Carousel: covers start `slide_from` slots away and settle in place. */
            Motion slide{0.24};
            double slide_from = 0;
            /* Grid: the focus ring glides from the previous cover. */
            Motion focus_glide{0.16};
            double focus_from_x = 0, focus_from_y = 0, focus_x = 0, focus_y = 0;
            /* Windows rise and the screen behind them dims. */
            Motion window_rise{0.2};
            std::vector<SlidingElement> sliding;
            /* The whole screen fades in when it is entered. */
            Motion enter{0.28};
            bool has_entered = false;


            void Rebuild();
            void AddHeader();
            void AddCarousel();
            void AddGrid();
            void AddFooter();
            void AddGameWindow(const library::Game &game);
            void AddDrivesWindow();
            void AddMessageWindow();
            /* A pill button: filled with the A when it is the one A presses (`active`), outlined otherwise. */
            void AddActionButton(double x, double y, double w, double h, const std::string &label, bool active, bool enabled, bool ring, bool in_window);
            /* The dimmed screen and a rising panel; the caller adds the body. */
            void AddWindowPanel(double x, double y, double w, double h);

            void PaintCover(size_t index, double x, double y, double size, double opacity);
            double WindowOffset() const;
            pu::ui::elm::TextBlock::Ref Slide(pu::ui::elm::TextBlock::Ref text);

            void Animate();

            void Move(s32 delta);
            void MoveFocus(Focus focus);
            void LaunchSelected();
            void OpenWindow(Window window);
            const library::Game *GetSelectedGame() const;

        public:
            explicit LibraryScreen(Actions actions);
            PU_SMART_CTOR(LibraryScreen)

            /* Keeps the same game selected when it is still in the list (a refresh, a new sort order). */
            void Show(std::vector<library::Game> games, std::vector<usbl::DriveStatus> drives, std::string message, Browse browse);
            void OnInput(u64 keys_down);
            /* Shows `text` in a window in the theme's colors, with the selected game's icon next to the title when
             * `with_game_icon`, and an action button when `action` has a label. */
            void ShowMessage(const std::string &title, const std::string &text, bool with_game_icon, MessageAction action = {});
            /* Plays the entrance (fade, and the covers gliding in the first time). */
            void Enter();
    };

}
