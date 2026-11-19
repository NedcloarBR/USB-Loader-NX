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
                About,

                Count,
            };

            enum class Focus {
                Sections,
                Content,
                ThemePicker,
            };

            std::function<void()> on_back;
            Section section = Section::Home;
            Focus focus = Focus::Sections;
            s32 selected = 0;

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

            /* The theme picker: a row of previews over the settings; A applies the focused one, B closes it. */
            std::vector<theme::Preview> themes;
            std::vector<pu::sdl2::TextureHandle::Ref> theme_thumbnails;
            s32 theme_selected = 0;
            s32 theme_first = 0;
            Motion picker_rise{0.2};

            void Rebuild();
            void AddSections();
            /* The toggle cards of the current section; empty for About. */
            std::span<const SettingOption> GetOptions() const;
            void AddOptions();
            void AddAboutSection();
            void AddFooter();
            void AddThemePicker();
            void OpenThemePicker();
            void OnThemePickerInput(u64 keys_down);

            void SelectSection(Section section);
            void SelectCard(s32 index);
            void ToggleSelected();

        public:
            explicit SettingsScreen(std::function<void()> on_back);
            PU_SMART_CTOR(SettingsScreen)

            void Show();
            void OnInput(u64 keys_down);
    };

}
