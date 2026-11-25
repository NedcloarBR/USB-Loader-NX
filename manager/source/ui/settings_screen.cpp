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
#include <ui/settings_screen.hpp>
#include <ui/animated_image.hpp>
#include <ui/components.hpp>
#include <art.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <span>
#include <string>
#include <vector>
#include <game_library.hpp>
#include <sound.hpp>
#include <strings.hpp>
#include <usbl/version.hpp>

namespace ui {

    /* A toggle card: a config.ini option of the module, or one of the Manager's own. */
    struct SettingOption {
        strings::Id label;
        strings::Id help;
        bool (*is_on)();
        bool (*set)(bool on);
        /* Off when the option only matters while another one is on. */
        bool (*is_available)();
        /* A choice rather than an on/off switch: its current value, shown on the card; A moves to the next one (set). */
        std::string (*value)() = nullptr;
        /* Shown in place of the help while the option is unavailable. */
        strings::Id unavailable_help = strings::Id::SettingNeedsAutoRegister;
        /* A slider from 0 to 100 rather than a switch: Left and Right move it by SliderStep. */
        s32 (*slider)() = nullptr;
        bool (*set_slider)(s32 value) = nullptr;
    };

    namespace {

        using strings::Id;

        bool IsModuleOptionOn(usbl::ConfigKey key) {
            usbl::ConfigValue value = {};
            return R_SUCCEEDED(usbl::GetConfigValue(&value, key)) && value.enabled;
        }

        template<usbl::ConfigKey Key>
        constexpr SettingOption ModuleOption(Id label, Id help, bool (*is_available)() = [] { return true; }) {
            return { label, help, [] { return IsModuleOptionOn(Key); }, [](bool on) { return R_SUCCEEDED(usbl::SetConfigValue(Key, on)); }, is_available };
        }

        constexpr SettingOption HomeOptions[] = {
            ModuleOption<usbl::ConfigKey::AutoRegister>(Id::SettingAutoRegister, Id::SettingAutoRegisterHelp),
            ModuleOption<usbl::ConfigKey::HideWhenRemoved>(Id::SettingHideWhenRemoved, Id::SettingHideWhenRemovedHelp, [] { return IsModuleOptionOn(usbl::ConfigKey::AutoRegister); }),
            ModuleOption<usbl::ConfigKey::Badge>(Id::SettingBadge, Id::SettingBadgeHelp),
            ModuleOption<usbl::ConfigKey::Notifications>(Id::SettingNotifications, Id::SettingNotificationsHelp),
            /* Protocol 1.1: a 1.0 module has no such option. */
            { Id::SettingHomebrewAccess, Id::SettingHomebrewAccessHelp, [] { return IsModuleOptionOn(usbl::ConfigKey::HomebrewAccess); },
              [](bool on) { return R_SUCCEEDED(usbl::SetConfigValue(usbl::ConfigKey::HomebrewAccess, on)); }, [] { return usbl::GetModuleMinorVersion() >= 1; },
              nullptr, Id::SettingNeedsNewerModule },
        };

        /* Empty: the console's language. Each language is named in itself. */
        struct LanguageChoice {
            const char *locale;
            const char *name;
        };
        constexpr LanguageChoice Languages[] = { { "", nullptr }, { "en-US", "English" }, { "pt-BR", "Português (Brasil)" } };

        size_t CurrentLanguage() {
            std::string locale;
            if (R_FAILED(usbl::GetLanguage(&locale))) {
                return 0;
            }
            const auto found = std::find_if(std::begin(Languages), std::end(Languages), [&](const LanguageChoice &choice) { return locale == choice.locale; });
            return found != std::end(Languages) ? static_cast<size_t>(found - std::begin(Languages)) : 0;
        }

        std::string LanguageName() {
            const auto &choice = Languages[CurrentLanguage()];
            return choice.name != nullptr ? choice.name : strings::Get(Id::SettingLanguageSystem);
        }

        /* The module writes config.ini and switches its notifications; the Manager reloads its own strings. */
        bool NextLanguage() {
            const std::string next = Languages[(CurrentLanguage() + 1) % std::size(Languages)].locale;
            if (R_FAILED(usbl::SetLanguage(next))) {
                return false;
            }
            strings::Initialize(next);
            return true;
        }

        /* The download while it runs, else how many games have art. */
        std::string ArtStatusText() {
            if (const art::Status status = art::GetStatus(); status.running) {
                return strings::Format(Id::ArtStatusRunning, { { "DONE", std::to_string(status.done) }, { "TOTAL", std::to_string(status.total) } });
            }
            const art::Report report = art::GetReport();
            if (report.total == 0) {
                return "—";
            }
            return strings::Format(Id::ArtCount, { { "COUNT", std::to_string(report.with_art) }, { "TOTAL", std::to_string(report.total) } });
        }

        Id ArtReason(art::Missing reason) {
            switch (reason) {
                case art::Missing::NotOnGameTdb: return Id::ArtReasonNotOnGameTdb;
                case art::Missing::NotOnServer:  return Id::ArtReasonNotOnServer;
                case art::Missing::NoInternet:   return Id::ArtReasonNoInternet;
                default:                         return Id::ArtReasonFailed;
            }
        }

        /* What the art window shows, to rebuild it only when it changes. */
        std::string ArtSnapshot() {
            std::string snapshot = ArtStatusText();
            for (const auto &missing : art::GetReport().missing) {
                snapshot += "\n" + missing.name + static_cast<char>('0' + static_cast<int>(missing.reason));
            }
            return snapshot;
        }

        constexpr SettingOption ManagerOptions[] = {
            /* Protocol 1.1. */
            { Id::SettingLanguage, Id::SettingLanguageHelp, [] { return false; }, [](bool) { return NextLanguage(); }, [] { return usbl::GetModuleMinorVersion() >= 1; },
              LanguageName, Id::SettingNeedsNewerModule },
            { Id::SettingSounds, Id::SettingSoundsHelp, sound::IsEnabled, sound::SetEnabled, [] { return true; } },
            { Id::SettingTheme, Id::SettingThemeHelp, [] { return false; }, [](bool) { return true; }, [] { return true; },
              [] { return theme::Current().empty() ? strings::Get(Id::SettingThemeDefault) : theme::DisplayName(); } },
            { Id::SettingMusic, Id::SettingMusicHelp, sound::IsMusicEnabled, sound::SetMusicEnabled, [] { return !theme::MusicPath().empty(); }, nullptr,
              Id::SettingMusicUnavailable },
            { Id::SettingMusicVolume, Id::SettingMusicVolumeHelp, [] { return false; }, [](bool) { return true; },
              [] { return !theme::MusicPath().empty() && sound::IsMusicEnabled(); }, nullptr, Id::SettingMusicUnavailable,
              sound::GetMusicVolume, sound::SetMusicVolume },
            { Id::SettingBackdrop, Id::SettingBackdropHelp, theme::IsGameBackdropEnabled, theme::SetGameBackdropEnabled, [] { return true; } },
            { Id::SettingShelves, Id::SettingShelvesHelp, [] { return false; }, [](bool) { return theme::SetShelvesByGenre(!theme::ShelvesByGenre()); }, [] { return true; },
              [] { return strings::Get(theme::ShelvesByGenre() ? Id::ShelvesByGenre : Id::ShelvesByFolder); } },
            { Id::SettingArt, Id::SettingArtHelp, art::IsEnabled, art::SetEnabled, [] { return true; } },
            { Id::SettingArtStatus, Id::SettingArtStatusHelp, [] { return false; }, [](bool) { art::Retry(); return true; }, art::IsEnabled,
              ArtStatusText, Id::SettingArtUnavailable },
        };

        constexpr Id SectionNames[] = { Id::SectionHome, Id::SectionManager, Id::SectionDiagnostics, Id::SectionAbout };
        constexpr Id SectionHelps[] = { Id::SectionHomeHelp, Id::SectionManagerHelp, Id::SectionDiagnosticsHelp, Id::Count };

        /* "NSZ" for "games/RPG/Game.nsz": the extension in capitals, or NSZ/XCZ when the name has none. */
        std::string ExtensionOf(const std::string &path) {
            const size_t dot = path.rfind('.');
            if (dot == std::string::npos || dot + 1 == path.size() || path.find('/', dot) != std::string::npos) {
                return "NSZ/XCZ";
            }
            std::string extension = path.substr(dot + 1);
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return extension;
        }

        std::string LeftOutReasonText(usbl::LeftOutReason reason, const std::string &path) {
            switch (reason) {
                case usbl::LeftOutReason::PathTooLong:      return strings::Get(Id::DriveLeftOutPathTooLong);
                case usbl::LeftOutReason::FolderTooDeep:    return strings::Get(Id::DriveLeftOutTooDeep);
                case usbl::LeftOutReason::TooManyEntries:   return strings::Get(Id::DriveLeftOutTooManyEntries);
                case usbl::LeftOutReason::IndexFull:        return strings::Get(Id::DriveLeftOutIndexFull);
                case usbl::LeftOutReason::SolidCompression: return strings::Format(Id::DriveLeftOutSolid, { { "EXT", ExtensionOf(path) } });
            }
            return {};
        }

        /* A column of sections on the left, the section's cards on the right, the button hints at the bottom. */
        /* The theme can move the column of sections and the content (Layout.Settings.sections / .content); these
         * follow it on every rebuild (UpdateMetrics). */
        constexpr double SectionH = 60, SectionStep = 66;
        double RailX = 0, RailW = 360, SectionsX = 32, SectionsW = 296, SectionsY = 112;
        double ContentX = 416, ContentW = DesignWidth - 416 - 56, CardsY = 122;
        constexpr double CardH = 82, CardGap = 10;
        constexpr double SliderW = 260;
        constexpr s32 SliderStep = 5;
        constexpr double FooterCenterY = DesignHeight - 30;

        double SectionY(s32 index) {
            return SectionsY + index * SectionStep;
        }

        /* `slot` counts from the first card shown. */
        double CardY(s32 slot) {
            return CardsY + slot * (CardH + CardGap);
        }

        s32 VisibleCards = 5;
        /* The settings' own title, placed with the rest. */
        components::Placement TitlePlace;

        void UpdateMetrics() {
            namespace cmp = components;
            const auto sections = cmp::Place("Settings", "sections", { true, 32, 112, 296, static_cast<double>(4 * 66) });
            SectionsX = sections.x;
            SectionsY = sections.y;
            SectionsW = sections.w;
            RailX = std::max(0.0, SectionsX - 32);
            RailW = SectionsW + 64;
            const auto content = cmp::Place("Settings", "content", { true, 416, 122, DesignWidth - 416 - 56, DesignHeight - 76 - 122 });
            ContentX = content.x;
            ContentW = content.w;
            CardsY = content.y;
            VisibleCards = std::max<s32>(1, static_cast<s32>((content.h + CardGap) / (CardH + CardGap)));
            TitlePlace = cmp::Place("Settings", "title", { true, SectionsX + 16, 42, SectionsW, 40, 30 });
        }

        /* More of a list above or below: a chevron in the primary color that nudges that way. */
        void MoreHint(double cx, double cy, bool down) {
            const double nudge = Pulse(1.2) * 4 * (down ? 1 : -1);
            draw::Circle(cx, cy + nudge, 15, color::WithAlpha(color::Primary, 0x30));
            draw::ChevronVertical(cx, cy + nudge, 22, down, color::Primary);
        }

        /* `on` from 0 (off) to 1 (on); in between while the knob slides. */
        void Switch(double x, double cy, double on) {
            draw::RoundRect(x, cy - 17, 60, 34, 17, draw::Mix(color::Track, color::Primary, on));
            draw::Circle(x + 17 + 26 * on, cy, 13, draw::Mix(color::TextSoft, color::OnPrimary, on));
        }

        void Veil(double progress, double x, double y, double w, double h) {
            if (progress < 1.0) {
                draw::Rect(x, y, w, h, color::WithAlpha(color::Ground, static_cast<u8>((1.0 - progress) * 255)));
            }
        }

    }

    std::span<const SettingOption> SettingsScreen::GetOptions() const {
        switch (this->section) {
            case Section::Home:    return HomeOptions;
            case Section::Manager: return ManagerOptions;
            default:               return {};
        }
    }

    SettingsScreen::SettingsScreen(std::function<void()> on_back) : Layout(), on_back(std::move(on_back)) {
        this->app_icon = library::LoadRoundedImage("romfs:/icon.jpg");
    }

    void SettingsScreen::Show() {
        this->focus = Focus::Sections;
        this->enter.Start();
        this->Rebuild();
    }

    void SettingsScreen::Rebuild() {
        this->Clear();
        this->taps.clear();
        UpdateMetrics();
        this->SetBackgroundColor(color::Ground);
        AddPaint(*this, []() {
            draw::Background();
            draw::Rect(RailX, 0, RailW, DesignHeight, color::WithAlpha(color::Panel, 0x80));
        });
        if (TitlePlace.visible) {
            AddText(*this, components::TextX(TitlePlace), TitlePlace.y, strings::Get(Id::SettingsTitle), TitlePlace.font_size, color::Text, TitlePlace.align, TitlePlace.w);
        }

        this->AddSections();
        const s32 section = static_cast<s32>(this->section);
        AddText(*this, ContentX, CardsY - 78, strings::Get(SectionNames[section]), 26, color::Text, Align::Left, ContentW);
        if (SectionHelps[section] != Id::Count) {
            AddText(*this, ContentX, CardsY - 38, strings::Get(SectionHelps[section]), 15, color::Muted, Align::Left, ContentW);
        }
        if (!this->GetOptions().empty()) {
            this->AddOptions();
        } else if (this->section == Section::Diagnostics) {
            this->AddDiagnosticsSection();
        } else {
            this->AddAboutSection();
        }
        AddPaint(*this, [this]() { Veil(this->content_fade.Progress(), ContentX - 16, CardsY - 86, ContentW + 32, DesignHeight - CardsY + 22); });
        this->AddFooter();

        if (this->focus == Focus::ThemePicker) {
            this->AddThemePicker();
        } else if (this->focus == Focus::ArtWindow) {
            this->AddArtWindow();
        }

        AddPaint(*this, [this]() { Veil(this->enter.Progress(), 0, 0, DesignWidth, DesignHeight); });
    }

    void SettingsScreen::AddSections() {
        AddPaint(*this, [this]() {
            const double y = this->section_glide.Between(this->section_from_y, SectionY(static_cast<s32>(this->section)));
            if (!draw::Skinned(theme::Skin::CardFocused, SectionsX, y, SectionsW, SectionH)) {
                draw::RoundRect(SectionsX, y, SectionsW, SectionH, 16, color::Raised);
            }
            draw::Chevron(SectionsX + SectionsW - 26, y + SectionH / 2, 18, color::Primary);
            if (this->focus == Focus::Sections) {
                draw::FocusRing(SectionsX, y, SectionsW, SectionH, 16, 4);
            }
        });
        for (s32 i = 0; i < static_cast<s32>(Section::Count); ++i) {
            this->taps.push_back({ SectionsX, SectionY(i), SectionsW, SectionH, [this, i]() {
                if (i != static_cast<s32>(this->section)) {
                    this->focus = Focus::Sections;
                    this->SelectSection(static_cast<Section>(i));
                }
            } });
            const bool active = i == static_cast<s32>(this->section);
            AddText(*this, SectionsX + 18, SectionY(i) + 18, strings::Get(SectionNames[i]), 18, active ? color::Text : color::Muted, Align::Left, SectionsW - 60);
        }
    }

    void SettingsScreen::AddOptions() {
        const auto options = this->GetOptions();
        const bool content_focused = this->focus == Focus::Content;
        const s32 count = static_cast<s32>(options.size());
        for (s32 i = this->first_card; i < std::min(count, this->first_card + VisibleCards); ++i) {
            const auto &option = options[i];
            const double y = CardY(i - this->first_card);
            /* A tap picks the card and does what A does; on a slider it sets the value where it was tapped. */
            this->taps.push_back({ ContentX, y, ContentW, CardH, [this, i]() {
                this->focus = Focus::Content;
                if (i != this->selected) {
                    this->SelectCard(i);
                }
                this->ToggleSelected();
            } });
            if (option.slider != nullptr) {
                const double bar_x = ContentX + ContentW - 34 - SliderW;
                this->taps.push_back({ bar_x - 16, y, SliderW + 32, CardH, [this, i, bar_x]() {
                    const auto &slider = this->GetOptions()[i];
                    if (!slider.is_available()) {
                        return;
                    }
                    this->focus = Focus::Content;
                    this->selected = i;
                    const s32 value = std::clamp(static_cast<s32>(std::lround((this->tap_x - bar_x) / SliderW * 100 / SliderStep)) * SliderStep, 0, 100);
                    if (slider.set_slider(value)) {
                        sound::Play(sound::Effect::Move);
                    }
                    this->Rebuild();
                } });
            }
            const bool on = option.is_on();
            const bool available = option.is_available();
            const bool toggled = i == this->toggled;
            const bool is_choice = option.value != nullptr;
            const bool is_slider = option.slider != nullptr;
            const double level = is_slider ? option.slider() / 100.0 : 0.0;
            const bool focused = content_focused && i == this->selected;
            AddPaint(*this, [this, y, on, available, toggled, is_choice, is_slider, level, focused]() {
                if (!draw::Skinned(focused ? theme::Skin::CardFocused : theme::Skin::Card, ContentX, y, ContentW, CardH)) {
                    draw::RoundRect(ContentX, y, ContentW, CardH, 18, focused ? color::Raised : color::Panel);
                }
                if (is_slider) {
                    const double x = ContentX + ContentW - 34 - SliderW, cy = y + CardH / 2;
                    draw::RoundRect(x, cy - 4, SliderW, 8, 4, color::Track);
                    if (level > 0) {
                        draw::RoundRect(x, cy - 4, std::max(8.0, SliderW * level), 8, 4, color::Primary);
                    }
                    draw::Circle(x + SliderW * level, cy, 12, color::Text);
                } else if (is_choice) {
                    draw::Chevron(ContentX + ContentW - 30, y + CardH / 2, 18, color::Primary);
                } else {
                    const double target = on ? 1.0 : 0.0;
                    Switch(ContentX + ContentW - 22 - 60, y + CardH / 2, toggled ? this->toggle.Between(1.0 - target, target) : target);
                }
                /* Unavailable: the whole card is faded. */
                if (!available) {
                    draw::RoundRect(ContentX, y, ContentW, CardH, 18, color::WithAlpha(color::Ground, 0x90));
                }
            });
            if (is_choice) {
                AddText(*this, ContentX + ContentW - 52, y + 30, option.value(), 17, color::Primary, Align::Right, 240);
            }
            if (is_slider) {
                AddText(*this, ContentX + ContentW - 34 - SliderW - 22, y + 30, std::to_string(option.slider()) + "%", 17, available ? color::Primary : color::Faint, Align::Right);
            }
            AddText(*this, ContentX + 22, y + 16, strings::Get(option.label), 18, available ? color::Text : color::Faint, Align::Left, ContentW - (is_choice ? 320 : (is_slider ? SliderW + 150 : 130)));
            AddText(*this, ContentX + 22, y + 46, strings::Get(available ? option.help : option.unavailable_help), 14, available ? color::Muted : color::Faint, Align::Left, ContentW - 130);
        }

        /* More cards above or below. */
        if (this->first_card > 0) {
            AddPaint(*this, []() { MoreHint(ContentX + ContentW / 2, CardsY - 14, false); });
        }
        if (this->first_card + VisibleCards < count) {
            AddPaint(*this, []() { MoreHint(ContentX + ContentW / 2, CardY(VisibleCards) + 4, true); });
        }

        if (content_focused) {
            AddPaint(*this, [this]() { draw::FocusRing(ContentX, this->focus_glide.Between(this->focus_from_y, CardY(this->selected - this->first_card)), ContentW, CardH, 18, 4); });
        }
    }

    /* What the drives' indexing left out (protocol 1.2), one card each: the file, why, and on which drive. */
    void SettingsScreen::AddDiagnosticsSection() {
        constexpr double ItemH = 92, ItemGap = 10, ListBottom = DesignHeight - 76;
        if (usbl::GetModuleMinorVersion() < 2) {
            AddText(*this, ContentX + 4, CardsY, strings::Get(Id::DiagnosticsNeedsNewerModule), 17, color::Muted, Align::Left, ContentW - 8);
            return;
        }

        std::vector<usbl::LeftOutFile> files;
        std::vector<usbl::DriveStatus> drives;
        usbl::ListLeftOutFiles(&files);
        usbl::ListDrives(&drives);
        this->diagnostics_snapshot.assign(reinterpret_cast<const char *>(files.data()), files.size() * sizeof(usbl::LeftOutFile));
        if (files.empty()) {
            AddText(*this, ContentX + 4, CardsY, strings::Get(Id::DiagnosticsNone), 17, color::Muted, Align::Left, ContentW - 8);
            return;
        }

        const s32 fits = static_cast<s32>((ListBottom - CardsY + ItemGap) / (ItemH + ItemGap));
        const s32 shown = static_cast<s32>(files.size()) > fits ? fits - 1 : static_cast<s32>(files.size());
        for (s32 i = 0; i < shown; ++i) {
            const auto &file = files[i];
            const double y = CardsY + i * (ItemH + ItemGap);
            AddPaint(*this, [y]() {
                if (!draw::Skinned(theme::Skin::Card, ContentX, y, ContentW, ItemH)) {
                    draw::RoundRect(ContentX, y, ContentW, ItemH, 18, color::Panel);
                }
                draw::IconWarning(ContentX + 30, y + ItemH / 2, 18, color::Alert);
            });

            /* The file's full path on the drive, then the drive, then why. */
            const std::string path(file.path, strnlen(file.path, sizeof(file.path)));
            const std::string full_path = path.empty() ? std::string() : "/usbloadernx/games/" + path;
            const auto drive = std::find_if(drives.begin(), drives.end(), [&](const usbl::DriveStatus &status) { return status.drive == file.drive; });
            const std::string where = drive != drives.end() ? std::string(drive->name, strnlen(drive->name, sizeof(drive->name))) : std::string();
            AddText(*this, ContentX + 56, y + 12, full_path.empty() ? where : full_path, 17, color::Text, Align::Left, ContentW - 80);
            AddText(*this, ContentX + 56, y + 38, full_path.empty() ? std::string() : where, 14, color::Muted, Align::Left, ContentW - 80);
            AddText(*this, ContentX + 56, y + 62, LeftOutReasonText(file.reason, path), 14, color::AlertText, Align::Left, ContentW - 80);
        }
        if (shown < static_cast<s32>(files.size())) {
            AddText(*this, ContentX + 4, CardsY + shown * (ItemH + ItemGap) + 12, strings::Format(Id::DriveLeftOutMore, { { "COUNT", std::to_string(files.size() - shown) } }), 15, color::Muted, Align::Left, ContentW - 8);
        }
    }

    void SettingsScreen::Refresh() {
        this->Rebuild();
    }

    void SettingsScreen::Poll() {
        /* The art download's progress, while the Manager section shows it. */
        if (this->section == Section::Manager && this->focus != Focus::ThemePicker && art::IsEnabled()) {
            if (const std::string status = ArtSnapshot(); status != this->art_snapshot) {
                this->art_snapshot = status;
                this->Rebuild();
            }
            return;
        }
        if (this->section != Section::Diagnostics || this->focus == Focus::ThemePicker || usbl::GetModuleMinorVersion() < 2) {
            return;
        }
        std::vector<usbl::LeftOutFile> files;
        usbl::ListLeftOutFiles(&files);
        if (std::string(reinterpret_cast<const char *>(files.data()), files.size() * sizeof(usbl::LeftOutFile)) != this->diagnostics_snapshot) {
            this->Rebuild();
        }
    }

    void SettingsScreen::AddAboutSection() {
        constexpr double IconSize = 136, CardHeight = 200;
        const double TextX = ContentX + 28 + IconSize + 32, TextW = ContentW - (TextX - ContentX) - 28;
        AddPaint(*this, [this]() {
            if (!draw::Skinned(theme::Skin::Card, ContentX, CardsY - 38, ContentW, CardHeight)) {
                draw::RoundRect(ContentX, CardsY - 38, ContentW, CardHeight, 18, color::Panel);
            }
            const double y = CardsY - 38 + (CardHeight - IconSize) / 2 + Float(3);
            if (this->app_icon != nullptr) {
                draw::Image(this->app_icon->Get(), ContentX + 28, y, IconSize, IconSize);
            }
        });
        const double Top = CardsY - 38;
        AddText(*this, TextX, Top + 30, strings::Get(Id::AppTitle), 28, color::Text);
        AddText(*this, TextX, Top + 72, strings::Format(Id::AboutVersion, { { "VERSION", usbl::Version } }), 17, color::Muted);
        const auto summary = WrapLines(strings::Get(Id::AboutSummary), 19, TextW, 2);
        for (size_t line = 0; line < summary.size(); ++line) {
            AddText(*this, TextX, Top + 106 + line * 26, summary[line], 19, color::TextSoft);
        }

        AddText(*this, ContentX + 4, Top + CardHeight + 28, strings::Get(Id::AboutLicense), 17, color::Muted, Align::Left, ContentW - 8);
        AddText(*this, ContentX + 4, Top + CardHeight + 56, "github.com/NedcloarBR/USB-Loader-NX", 17, color::Info);
        AddText(*this, ContentX + 4, Top + CardHeight + 92, strings::Get(Id::AboutArtCredit), 15, color::Muted, Align::Left, ContentW - 8);
    }

    namespace {

        /* The theme picker, in design px: the list at the left, the preview at the right. */
        constexpr double PickerX = 48, PickerY = 32, PickerW = DesignWidth - 2 * PickerX, PickerH = DesignHeight - 60 - 2 * PickerY + 24;
        constexpr double PickerPad = 32, ListX = PickerX + PickerPad, ListY = PickerY + 92, ListW = 360;
        constexpr double RowH = 56, RowGap = 8, RowStep = RowH + RowGap;
        constexpr s32 VisibleRows = 7;
        constexpr double PreviewX = ListX + ListW + PickerPad, PreviewW = PickerX + PickerW - PickerPad - PreviewX, PreviewH = PreviewW * 9 / 16, PreviewY = ListY;

        double RowY(s32 slot) {
            return ListY + slot * RowStep;
        }

        /* No picture: a sketch of the home screen in the theme's colors, laid out on a 224 x 126 card and scaled. */
        void SketchTheme(const theme::Preview &preview, double x, double y, double w) {
            const double k = w / 224;
            draw::RoundRect(x, y, w, 126 * k, 14 * k, preview.ground);
            draw::RoundRect(x + 10 * k, y + 22 * k, 50 * k, 50 * k, 8 * k, preview.raised);
            draw::RoundOutline(x + 7 * k, y + 19 * k, 56 * k, 56 * k, 10 * k, 2.5 * k, preview.primary);
            for (s32 i = 0; i < 3; ++i) {
                draw::RoundRect(x + (72 + i * 44) * k, y + 38 * k, 38 * k, 34 * k, 6 * k, preview.panel);
            }
            draw::RoundRect(x + 10 * k, y + 88 * k, 90 * k, 7 * k, 3.5 * k, preview.text);
            draw::RoundRect(x + w - 64 * k, y + 98 * k, 52 * k, 16 * k, 8 * k, preview.primary);
        }

        std::vector<std::string> ThemeFeatures(const theme::Preview &preview) {
            std::vector<std::string> features;
            const auto add = [&features](bool has, Id id) {
                if (has) {
                    features.push_back(strings::Get(id));
                }
            };
            add(preview.animated, Id::ThemeFeatureAnimated);
            add(preview.background, Id::ThemeFeatureBackground);
            add(preview.music, Id::ThemeFeatureMusic);
            add(preview.sounds, Id::ThemeFeatureSounds);
            add(preview.font, Id::ThemeFeatureFont);
            add(preview.layout, Id::ThemeFeatureLayout);
            add(preview.images, Id::ThemeFeatureImages);
            if (features.empty()) {
                features.push_back(strings::Get(Id::ThemeFeatureColors));
            }
            return features;
        }

    }

    void SettingsScreen::OpenThemePicker() {
        this->themes = theme::ListPreviews();
        this->theme_thumbnails.clear();
        for (const auto &preview : this->themes) {
            /* An animated background shows its first frame (SDL_image cannot open an animated WebP). */
            SDL_Texture *texture = preview.thumbnail.empty() ? nullptr : AnimatedImage::LoadFirstFrame(preview.thumbnail);
            if (texture == nullptr && !preview.thumbnail.empty()) {
                texture = pu::ui::render::LoadImageFromFile(preview.thumbnail);
            }
            this->theme_thumbnails.push_back(texture != nullptr ? pu::sdl2::TextureHandle::New(texture) : pu::sdl2::TextureHandle::Ref());
        }
        const auto current = std::find_if(this->themes.begin(), this->themes.end(), [](const theme::Preview &preview) { return preview.name == theme::Current(); });
        this->theme_selected = current != this->themes.end() ? static_cast<s32>(current - this->themes.begin()) : 0;
        this->theme_first = std::max(0, this->theme_selected - VisibleRows + 1);
        this->picker_from_y = RowY(this->theme_selected - this->theme_first);
        this->focus = Focus::ThemePicker;
        this->picker_rise.Start();
        sound::Play(sound::Effect::Open);
        this->Rebuild();
    }

    void SettingsScreen::AddThemePicker() {
        const s32 count = static_cast<s32>(this->themes.size());
        this->theme_first = std::clamp(this->theme_first, std::max(0, this->theme_selected - VisibleRows + 1), this->theme_selected);

        AddPaint(*this, [this]() {
            draw::Rect(0, 0, DesignWidth, DesignHeight, color::WithAlpha(color::Overlay, static_cast<u8>(color::Overlay.a * this->picker_rise.Progress())));
            draw::Translate rise(0, (1.0 - this->picker_rise.Progress()) * 36);
            if (!draw::Skinned(theme::Skin::Panel, PickerX, PickerY, PickerW, PickerH)) {
                draw::RoundRect(PickerX, PickerY, PickerW, PickerH, 30, color::Panel);
            }
        });
        AddText(*this, ListX, PickerY + 32, strings::Get(Id::SettingTheme), 26, color::Text);

        /* Left: one row per theme; the one in use is marked. */
        this->taps.push_back({ 0, 0, DesignWidth, DesignHeight, [this]() { this->OnInput(HidNpadButton_B); } });
        this->taps.push_back({ PickerX, PickerY, PickerW, PickerH, []() {} });
        for (s32 slot = 0; slot < VisibleRows && this->theme_first + slot < count; ++slot) {
            const s32 index = this->theme_first + slot;
            const bool focused = index == this->theme_selected;
            const double y = RowY(slot);
            /* The first tap picks a theme, the second applies it. */
            this->taps.push_back({ ListX, y, ListW, RowH, [this, index]() {
                if (index == this->theme_selected) {
                    this->OnInput(HidNpadButton_A);
                } else {
                    this->OnInput(index < this->theme_selected ? HidNpadButton_AnyUp : HidNpadButton_AnyDown);
                    while (this->theme_selected != index) {
                        this->OnInput(index < this->theme_selected ? HidNpadButton_AnyUp : HidNpadButton_AnyDown);
                    }
                }
            } });
            AddPaint(*this, [this, y, focused]() {
                draw::Translate rise(0, (1.0 - this->picker_rise.Progress()) * 36);
                if (!draw::Skinned(focused ? theme::Skin::CardFocused : theme::Skin::Card, ListX, y, ListW, RowH)) {
                    draw::RoundRect(ListX, y, ListW, RowH, 16, focused ? color::Raised : color::WithAlpha(color::Raised, 0x60));
                }
            });
            const auto &preview = this->themes[index];
            const bool in_use = preview.name == theme::Current();
            const std::string in_use_text = strings::Get(Id::ThemeInUse);
            const double in_use_w = in_use ? pu::ui::render::GetTextWidth(Font(14), in_use_text) / Scale + 16 : 0;
            const std::string name = preview.name.empty() ? strings::Get(Id::SettingThemeDefault) : preview.display_name;
            AddText(*this, ListX + 20, y + (RowH - 24) / 2, name, 18, focused ? color::Text : color::TextSoft, Align::Left, ListW - 40 - in_use_w);
            if (in_use) {
                AddText(*this, ListX + ListW - 20, y + (RowH - 20) / 2, in_use_text, 14, color::Primary, Align::Right);
            }
        }
        if (this->theme_first > 0) {
            AddPaint(*this, []() { MoreHint(ListX + ListW / 2, ListY - 14, false); });
        }
        if (this->theme_first + VisibleRows < count) {
            AddPaint(*this, []() { MoreHint(ListX + ListW / 2, RowY(VisibleRows) + 6, true); });
        }
        const double ring_y = RowY(this->theme_selected - this->theme_first);
        AddPaint(*this, [this, ring_y]() {
            draw::Translate rise(0, (1.0 - this->picker_rise.Progress()) * 36);
            draw::FocusRing(ListX, this->picker_glide.Between(this->picker_from_y, ring_y), ListW, RowH, 16, 4);
        });

        /* Right: the focused theme large, then its name, author and version, and what it changes. */
        if (count == 0) {
            return;
        }
        const s32 index = this->theme_selected;
        AddPaint(*this, [this, index]() {
            draw::Translate rise(0, (1.0 - this->picker_rise.Progress()) * 36);
            if (const auto &thumbnail = this->theme_thumbnails[index]; thumbnail != nullptr) {
                draw::Image(thumbnail->Get(), PreviewX, PreviewY, PreviewW, PreviewH);
            } else {
                SketchTheme(this->themes[index], PreviewX, PreviewY, PreviewW);
            }
        });
        const auto &preview = this->themes[index];
        const double info_y = PreviewY + PreviewH + 16;
        AddText(*this, PreviewX, info_y, preview.name.empty() ? strings::Get(Id::SettingThemeDefault) : preview.display_name, 26, color::Text, Align::Left, PreviewW);
        std::string byline;
        if (!preview.author.empty()) {
            byline = strings::Format(Id::ThemeBy, { { "AUTHOR", preview.author } });
        }
        if (!preview.version.empty()) {
            const std::string version = strings::Format(Id::ThemeVersion, { { "VERSION", preview.version } });
            byline = byline.empty() ? version : byline + "  ·  " + version;
        }
        if (!byline.empty()) {
            AddText(*this, PreviewX, info_y + 34, byline, 16, color::Muted, Align::Left, PreviewW);
        }

        const double chips_y = info_y + (byline.empty() ? 40 : 62);
        std::vector<std::pair<double, double>> boxes;
        const auto features = ThemeFeatures(preview);
        double chip_x = PreviewX;
        for (const auto &feature : features) {
            const double w = pu::ui::render::GetTextWidth(Font(14), feature) / Scale + 24;
            if (chip_x + w > PreviewX + PreviewW) {
                break;
            }
            boxes.push_back({ chip_x, w });
            chip_x += w + 8;
        }
        AddPaint(*this, [this, boxes, chips_y]() {
            draw::Translate rise(0, (1.0 - this->picker_rise.Progress()) * 36);
            for (const auto &[x, w] : boxes) {
                if (!draw::Skinned(theme::Skin::Chip, x, chips_y, w, 30)) {
                    draw::RoundRect(x, chips_y, w, 30, 8, color::Raised);
                }
            }
        });
        for (size_t i = 0; i < boxes.size(); ++i) {
            AddText(*this, boxes[i].first + 12, chips_y + 6, features[i], 14, color::TextSoft);
        }
    }

    void SettingsScreen::OnThemePickerInput(u64 keys_down) {
        const s32 count = static_cast<s32>(this->themes.size());
        const auto move = [this](s32 delta) {
            sound::Play(sound::Effect::Move);
            this->picker_from_y = this->picker_glide.Between(this->picker_from_y, RowY(this->theme_selected - this->theme_first));
            this->theme_selected += delta;
            /* The list scrolls under the ring: the ring glides from where the row it left now is. */
            const s32 first = std::clamp(this->theme_first, std::max(0, this->theme_selected - VisibleRows + 1), this->theme_selected);
            this->picker_from_y -= (first - this->theme_first) * RowStep;
            this->theme_first = first;
            this->picker_glide.Start();
            this->Rebuild();
        };
        if (keys_down & HidNpadButton_B) {
            sound::Play(sound::Effect::Back);
            this->focus = Focus::Content;
            this->Rebuild();
        } else if ((keys_down & HidNpadButton_AnyUp) && this->theme_selected > 0) {
            move(-1);
        } else if ((keys_down & HidNpadButton_AnyDown) && this->theme_selected < count - 1) {
            move(1);
        } else if (keys_down & HidNpadButton_A) {
            theme::Apply(this->themes[this->theme_selected].name);
            sound::Reload();
            sound::Play(sound::Effect::Select);
            this->Rebuild();
        }
    }

    void SettingsScreen::AddArtWindow() {
        constexpr double W = 800, X = (DesignWidth - W) / 2, Pad = 40, RowH = 34, ButtonH = 56;
        constexpr size_t MaxRows = 7;
        const art::Report report = art::GetReport();
        const size_t rows = std::min(report.missing.size(), MaxRows);
        const bool more = report.missing.size() > MaxRows;
        const double list_h = report.missing.empty() ? 40 : 36 + (rows + (more ? 1 : 0)) * RowH;
        const double H = Pad + 40 + 34 + 20 + list_h + 24 + ButtonH + Pad, Y = std::max(24.0, (DesignHeight - 60 - H) / 2);

        AddPaint(*this, [this, H, Y]() {
            draw::Rect(0, 0, DesignWidth, DesignHeight, color::WithAlpha(color::Overlay, static_cast<u8>(color::Overlay.a * this->picker_rise.Progress())));
            if (!draw::Skinned(theme::Skin::Panel, X, Y, W, H)) {
                draw::RoundRect(X, Y, W, H, 30, color::Panel);
            }
        });
        AddText(*this, X + Pad, Y + Pad - 4, strings::Get(Id::SettingArtStatus), 26, color::Text);
        AddText(*this, X + Pad, Y + Pad + 40, ArtStatusText(), 17, color::Primary, Align::Left, W - 2 * Pad);

        /* The games without a cover, and why. */
        double y = Y + Pad + 40 + 34 + 20;
        if (report.missing.empty()) {
            if (report.total > 0) {
                AddText(*this, X + Pad, y, strings::Get(Id::ArtAllHave), 16, color::Muted, Align::Left, W - 2 * Pad);
            }
        } else {
            AddText(*this, X + Pad, y, strings::Get(Id::ArtMissingTitle), 16, color::Muted);
            y += 36;
            for (size_t i = 0; i < rows; ++i) {
                const auto &missing = report.missing[i];
                const std::string reason = strings::Get(ArtReason(missing.reason));
                const double reason_w = pu::ui::render::GetTextWidth(Font(15), reason) / Scale;
                AddText(*this, X + Pad, y + i * RowH, missing.name, 16, color::Text, Align::Left, W - 2 * Pad - reason_w - 24);
                AddText(*this, X + W - Pad, y + i * RowH + 1, reason, 15, missing.reason == art::Missing::NotOnGameTdb ? color::Muted : color::Alert, Align::Right);
            }
            if (more) {
                AddText(*this, X + Pad, y + rows * RowH, strings::Format(Id::ArtMissingMore, { { "COUNT", std::to_string(report.missing.size() - rows) } }), 15, color::Muted);
            }
        }

        /* Left and Right pick one; A presses it. */
        this->taps.push_back({ 0, 0, DesignWidth, DesignHeight, [this]() { this->OnInput(HidNpadButton_B); } });
        this->taps.push_back({ X, Y, W, H, []() {} });
        const double button_y = Y + H - Pad - ButtonH;
        const std::string labels[] = { strings::Get(Id::ArtDownloadMissing), strings::Get(Id::ArtDelete) };
        double button_x = X + Pad;
        for (s32 i = 0; i < 2; ++i) {
            const double w = pu::ui::render::GetTextWidth(Font(18), labels[i]) / Scale + 64;
            this->taps.push_back({ button_x, button_y, w, ButtonH, [this, i]() {
                this->art_button = i;
                this->OnInput(HidNpadButton_A);
            } });
            const bool active = i == this->art_button;
            AddPaint(*this, [button_x, button_y, w, active]() {
                if (active) {
                    if (!draw::Skinned(theme::Skin::ButtonActive, button_x, button_y, w, ButtonH)) {
                        draw::RoundRect(button_x, button_y, w, ButtonH, ButtonH / 2, color::Primary);
                    }
                    draw::FocusRing(button_x, button_y, w, ButtonH, ButtonH / 2, 4);
                } else if (!draw::Skinned(theme::Skin::Button, button_x, button_y, w, ButtonH)) {
                    draw::RoundRect(button_x, button_y, w, ButtonH, ButtonH / 2, color::Raised);
                    draw::RoundOutline(button_x, button_y, w, ButtonH, ButtonH / 2, 1.5, color::Track);
                }
            });
            AddText(*this, button_x + w / 2, button_y + (ButtonH - 24) / 2, labels[i], 18, active ? color::OnPrimary : color::Text, Align::Center);
            button_x += w + 16;
        }
    }

    void SettingsScreen::OnArtWindowInput(u64 keys_down) {
        if (keys_down & HidNpadButton_B) {
            sound::Play(sound::Effect::Back);
            this->focus = Focus::Content;
            this->Rebuild();
        } else if (keys_down & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight)) {
            const s32 button = (keys_down & HidNpadButton_AnyLeft) ? 0 : 1;
            if (button != this->art_button) {
                sound::Play(sound::Effect::Move);
                this->art_button = button;
                this->Rebuild();
            }
        } else if (keys_down & HidNpadButton_A) {
            sound::Play(sound::Effect::Select);
            if (this->art_button == 0) {
                art::Retry();
            } else {
                art::DeleteAll();
            }
            this->Rebuild();
        }
    }

    void SettingsScreen::AddFooter() {
        if (this->focus == Focus::ThemePicker) {
            const double hints_x = AddHintBar(*this, DesignWidth - 48, FooterCenterY, { { "B", strings::Get(Id::HintBack) }, { "A", strings::Get(Id::HintApply) } }, color::TextSoft);
            this->taps.push_back({ hints_x, FooterCenterY - 24, DesignWidth - 48 - hints_x, 48, [this]() { this->OnInput(HidNpadButton_B); } });
            return;
        }
        if (this->focus == Focus::ArtWindow) {
            const double hints_x = AddHintBar(*this, DesignWidth - 48, FooterCenterY, { { "B", strings::Get(Id::HintBack) }, { "A", strings::Get(Id::HintSelect) } }, color::TextSoft);
            this->taps.push_back({ hints_x, FooterCenterY - 24, DesignWidth - 48 - hints_x, 48, [this]() { this->OnInput(HidNpadButton_B); } });
            return;
        }
        std::vector<Hint> hints = { { "B", strings::Get(Id::HintBack) } };
        if (this->focus == Focus::Content) {
            if (this->GetOptions()[this->selected].slider == nullptr) {
                hints.push_back({ "A", strings::Get(Id::HintToggle) });
            }
        } else if (!this->GetOptions().empty()) {
            hints.push_back({ "A", strings::Get(Id::HintSelect) });
        }
        const double hints_x = AddHintBar(*this, DesignWidth - 48, FooterCenterY, hints, color::Muted);
        this->taps.push_back({ hints_x, FooterCenterY - 24, DesignWidth - 48 - hints_x, 48, [this]() { this->OnInput(HidNpadButton_B); } });
    }

    void SettingsScreen::SelectSection(Section section) {
        this->section_from_y = this->section_glide.Between(this->section_from_y, SectionY(static_cast<s32>(this->section)));
        this->section_glide.Start();
        this->content_fade.Start();
        this->section = section;
        this->selected = 0;
        this->first_card = 0;
        this->toggled = -1;
        sound::Play(sound::Effect::Move);
        this->Rebuild();
    }

    void SettingsScreen::SelectCard(s32 index) {
        this->focus_from_y = this->focus_glide.Between(this->focus_from_y, CardY(this->selected - this->first_card));
        this->focus_glide.Start();
        this->selected = index;
        /* The cards scroll under the ring: it glides from where the card it left now is. */
        const s32 first = std::clamp(this->first_card, std::max(0, index - VisibleCards + 1), index);
        this->focus_from_y -= (first - this->first_card) * (CardH + CardGap);
        this->first_card = first;
        sound::Play(sound::Effect::Move);
        this->Rebuild();
    }

    void SettingsScreen::ToggleSelected() {
        const auto &option = this->GetOptions()[this->selected];
        if (!option.is_available()) {
            sound::Play(sound::Effect::Error);
            return;
        }
        if (option.label == Id::SettingTheme) {
            this->OpenThemePicker();
            return;
        }
        if (option.label == Id::SettingArtStatus) {
            sound::Play(sound::Effect::Open);
            this->art_button = 0;
            this->art_snapshot = ArtSnapshot();
            this->focus = Focus::ArtWindow;
            this->picker_rise.Start();
            this->Rebuild();
            return;
        }
        if (option.slider != nullptr) {
            return;
        }
        if (option.value != nullptr) {
            if (!option.set(true)) {
                sound::Play(sound::Effect::Error);
                return;
            }
            sound::Play(sound::Effect::Select);
            this->Rebuild();
            return;
        }
        const bool enabled = !option.is_on();
        if (!option.set(enabled)) {
            sound::Play(sound::Effect::Error);
            return;
        }
        sound::Play(enabled ? sound::Effect::ToggleOn : sound::Effect::ToggleOff);
        this->toggled = this->selected;
        this->toggle.Start();
        this->Rebuild();
    }

    void SettingsScreen::OnTouch(const pu::ui::TouchPoint &touch) {
        const auto event = this->gesture.Update(touch);
        if (event.kind == TouchGesture::Event::Kind::Swipe) {
            if (std::abs(event.y) > std::abs(event.x)) {
                if (this->focus == Focus::Sections && !this->GetOptions().empty()) {
                    this->OnInput(HidNpadButton_A);
                }
                this->OnInput(event.y < 0 ? HidNpadButton_AnyDown : HidNpadButton_AnyUp);
            }
        } else if (event.kind == TouchGesture::Event::Kind::Tap) {
            this->tap_x = event.x;
            Tap(this->taps, event.x, event.y);
        }
    }

    void SettingsScreen::OnInput(u64 keys_down) {
        if (this->focus == Focus::ThemePicker) {
            this->OnThemePickerInput(keys_down);
            return;
        }
        if (this->focus == Focus::ArtWindow) {
            this->OnArtWindowInput(keys_down);
            return;
        }
        const s32 section = static_cast<s32>(this->section);
        if (this->focus == Focus::Sections) {
            if (keys_down & HidNpadButton_B) {
                sound::Play(sound::Effect::Back);
                this->on_back();
            } else if ((keys_down & HidNpadButton_AnyUp) && section > 0) {
                this->SelectSection(static_cast<Section>(section - 1));
            } else if ((keys_down & HidNpadButton_AnyDown) && section < static_cast<s32>(Section::Count) - 1) {
                this->SelectSection(static_cast<Section>(section + 1));
            } else if ((keys_down & (HidNpadButton_A | HidNpadButton_AnyRight)) && !this->GetOptions().empty()) {
                sound::Play(sound::Effect::Select);
                this->focus = Focus::Content;
                this->focus_glide = Motion(0.16);
                this->Rebuild();
            }
            return;
        }

        /* On a slider, Left and Right move it; B still goes back. */
        if (const auto &option = this->GetOptions()[this->selected]; option.slider != nullptr && (keys_down & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight))) {
            if (!option.is_available()) {
                sound::Play(sound::Effect::Error);
                return;
            }
            const s32 value = std::clamp(option.slider() + ((keys_down & HidNpadButton_AnyLeft) ? -SliderStep : SliderStep), 0, 100);
            if (value != option.slider() && option.set_slider(value)) {
                sound::Play(sound::Effect::Move);
                this->Rebuild();
            }
            return;
        }
        if (keys_down & (HidNpadButton_B | HidNpadButton_AnyLeft)) {
            sound::Play(sound::Effect::Back);
            this->focus = Focus::Sections;
            this->Rebuild();
        } else if ((keys_down & HidNpadButton_AnyUp) && this->selected > 0) {
            this->SelectCard(this->selected - 1);
        } else if ((keys_down & HidNpadButton_AnyDown) && this->selected < static_cast<s32>(this->GetOptions().size()) - 1) {
            this->SelectCard(this->selected + 1);
        } else if (keys_down & HidNpadButton_A) {
            this->ToggleSelected();
        }
    }

}
