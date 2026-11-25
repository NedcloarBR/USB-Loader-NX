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
#include <span>
#include <usbl_client.hpp>
#include <ui/motion.hpp>
#include <ui/widgets.hpp>

namespace ui {

    struct SettingOption;

    /* Laid out like the console's System Settings: sections listed on the left (Up/Down), the chosen one's content on
     * the right (A or Right to enter it, B or Left to leave). config.ini options are toggle cards written through the module. */
    class SettingsScreen : public pu::ui::Layout {
        private:
            enum class Section {
                Home,
                Manager,
                Diagnostics,
                About,

                Count,
            };

            enum class Focus {
                Sections,
                Content,
                ThemePicker,
                ArtWindow,
            };

            std::function<void()> on_back;
            Section section = Section::Home;
            Focus focus = Focus::Sections;
            s32 selected = 0;
            /* The first card shown: a section with more cards than fit scrolls with the focus. */
            s32 first_card = 0;

            /* The section highlight and the card ring glide to their new place. */
            Motion section_glide{0.16};
            double section_from_y = 0;
            Motion focus_glide{0.16};
            double focus_from_y = 0;
            /* The knob of the last toggled switch slides across. */
            Motion toggle{0.18};
            s32 toggled = -1;
            /* The screen fades in when it is entered; the content fades in when the section changes. */
            Motion enter{0.28};
            Motion content_fade{0.18};

            pu::sdl2::TextureHandle::Ref app_icon;

            /* The theme picker: the themes as a list (Up/Down) next to a large preview of the focused one, over the
             * settings; A applies it, B closes the picker. */
            std::vector<theme::Preview> themes;
            std::vector<pu::sdl2::TextureHandle::Ref> theme_thumbnails;
            s32 theme_selected = 0;
            s32 theme_first = 0;
            Motion picker_rise{0.2};
            /* The focus glides along the list. */
            Motion picker_glide{0.16};
            double picker_from_y = 0;

            void Rebuild();
            void AddSections();
            /* The toggle cards of the current section; empty for About. */
            std::span<const SettingOption> GetOptions() const;
            void AddOptions();
            void AddAboutSection();
            void AddDiagnosticsSection();
            void AddFooter();
            void AddThemePicker();
            void OpenThemePicker();
            void OnThemePickerInput(u64 keys_down);
            /* The art window: how many games have art, the ones without and why, and its two actions. */
            void AddArtWindow();
            void OnArtWindowInput(u64 keys_down);
            /* 0: download what is missing, 1: delete the art. */
            s32 art_button = 0;
            TouchGesture gesture;
            std::vector<TapTarget> taps;
            /* Where the last tap was (a slider takes its value from it). */
            double tap_x = 0;

            void SelectSection(Section section);
            void SelectCard(s32 index);
            void ToggleSelected();

            /* What the Diagnostics section shows, to rebuild it only when the module's list changes. */
            std::string diagnostics_snapshot;
            /* The art download's status as last shown. */
            std::string art_snapshot;

        public:
            explicit SettingsScreen(std::function<void()> on_back);
            PU_SMART_CTOR(SettingsScreen)

            void Show();
            void OnInput(u64 keys_down);
            /* Every frame: a tap on a section, a card, a theme or a button does what A would there; a tap on the
             * button hints goes back; a swipe moves Up or Down. */
            void OnTouch(const pu::ui::TouchPoint &touch);
            /* Called every few seconds while the screen is shown: Diagnostics follows drives plugged in or removed. */
            void Poll();
            /* Draws everything again (the theme changed). */
            void Refresh();
    };

}
