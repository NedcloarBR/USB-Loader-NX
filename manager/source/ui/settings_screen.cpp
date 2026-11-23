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
        constexpr double RailW = 360, SectionsX = 32, SectionsW = RailW - 64, SectionH = 60, SectionStep = 66, SectionsY = 112;
        constexpr double ContentX = RailW + 56, ContentW = DesignWidth - ContentX - 56, CardsY = 122;
        constexpr double CardH = 82, CardGap = 10;
        constexpr double SliderW = 260;
        constexpr s32 SliderStep = 5;
        constexpr double FooterCenterY = DesignHeight - 30;

        constexpr double SectionY(s32 index) {
            return SectionsY + index * SectionStep;
        }

        constexpr double CardY(s32 index) {
            return CardsY + index * (CardH + CardGap);
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
        this->SetBackgroundColor(color::Ground);
        AddPaint(*this, []() {
            draw::Background();
            draw::Rect(0, 0, RailW, DesignHeight, color::WithAlpha(color::Panel, 0x80));
        });
        AddText(*this, SectionsX + 16, 42, strings::Get(Id::SettingsTitle), 30, color::Text);

        this->AddSections();
        const s32 section = static_cast<s32>(this->section);
        AddText(*this, ContentX, 44, strings::Get(SectionNames[section]), 26, color::Text);
        if (SectionHelps[section] != Id::Count) {
            AddText(*this, ContentX, 84, strings::Get(SectionHelps[section]), 15, color::Muted, Align::Left, ContentW);
        }
        if (!this->GetOptions().empty()) {
            this->AddOptions();
        } else if (this->section == Section::Diagnostics) {
            this->AddDiagnosticsSection();
        } else {
            this->AddAboutSection();
        }
        AddPaint(*this, [this]() { Veil(this->content_fade.Progress(), ContentX - 16, 36, ContentW + 32, DesignHeight - 100); });
        this->AddFooter();

        if (this->focus == Focus::ThemePicker) {
            this->AddThemePicker();
        }

        AddPaint(*this, [this]() { Veil(this->enter.Progress(), 0, 0, DesignWidth, DesignHeight); });
    }

    void SettingsScreen::AddSections() {
        AddPaint(*this, [this]() {
            const double y = this->section_glide.Between(this->section_from_y, SectionY(static_cast<s32>(this->section)));
            draw::RoundRect(SectionsX, y, SectionsW, SectionH, 16, color::Raised);
            draw::Chevron(SectionsX + SectionsW - 26, y + SectionH / 2, 18, color::Primary);
            if (this->focus == Focus::Sections) {
                draw::FocusRing(SectionsX, y, SectionsW, SectionH, 16, 4);
            }
        });
        for (s32 i = 0; i < static_cast<s32>(Section::Count); ++i) {
            const bool active = i == static_cast<s32>(this->section);
            AddText(*this, SectionsX + 18, SectionY(i) + 18, strings::Get(SectionNames[i]), 18, active ? color::Text : color::Muted, Align::Left, SectionsW - 60);
        }
    }

    void SettingsScreen::AddOptions() {
        const auto options = this->GetOptions();
        const bool content_focused = this->focus == Focus::Content;
        for (s32 i = 0; i < static_cast<s32>(options.size()); ++i) {
            const auto &option = options[i];
            const double y = CardY(i);
            const bool on = option.is_on();
            const bool available = option.is_available();
            const bool toggled = i == this->toggled;
            const bool is_choice = option.value != nullptr;
            const bool is_slider = option.slider != nullptr;
            const double level = is_slider ? option.slider() / 100.0 : 0.0;
            const bool focused = content_focused && i == this->selected;
            AddPaint(*this, [this, y, on, available, toggled, is_choice, is_slider, level, focused]() {
                draw::RoundRect(ContentX, y, ContentW, CardH, 18, focused ? color::Raised : color::Panel);
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

        if (content_focused) {
            AddPaint(*this, [this]() { draw::FocusRing(ContentX, this->focus_glide.Between(this->focus_from_y, CardY(this->selected)), ContentW, CardH, 18, 4); });
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
                draw::RoundRect(ContentX, y, ContentW, ItemH, 18, color::Panel);
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

    void SettingsScreen::Poll() {
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
        constexpr double TextX = ContentX + 28 + IconSize + 32, TextW = ContentW - (TextX - ContentX) - 28;
        AddPaint(*this, [this]() {
            draw::RoundRect(ContentX, CardsY - 38, ContentW, CardHeight, 18, color::Panel);
            const double y = CardsY - 38 + (CardHeight - IconSize) / 2 + Float(3);
            if (this->app_icon != nullptr) {
                draw::Image(this->app_icon->Get(), ContentX + 28, y, IconSize, IconSize);
            }
        });
        constexpr double Top = CardsY - 38;
        AddText(*this, TextX, Top + 30, strings::Get(Id::AppTitle), 28, color::Text);
        AddText(*this, TextX, Top + 72, strings::Format(Id::AboutVersion, { { "VERSION", usbl::Version } }), 17, color::Muted);
        const auto summary = WrapLines(strings::Get(Id::AboutSummary), 19, TextW, 2);
        for (size_t line = 0; line < summary.size(); ++line) {
            AddText(*this, TextX, Top + 106 + line * 26, summary[line], 19, color::TextSoft);
        }

        AddText(*this, ContentX + 4, Top + CardHeight + 28, strings::Get(Id::AboutLicense), 17, color::Muted, Align::Left, ContentW - 8);
        AddText(*this, ContentX + 4, Top + CardHeight + 56, "github.com/NedcloarBR/USB-Loader-NX", 17, color::Info);
    }

    void SettingsScreen::OpenThemePicker() {
        this->themes = theme::ListPreviews();
        this->theme_thumbnails.clear();
        for (const auto &preview : this->themes) {
            SDL_Texture *texture = preview.thumbnail.empty() ? nullptr : pu::ui::render::LoadImageFromFile(preview.thumbnail);
            this->theme_thumbnails.push_back(texture != nullptr ? pu::sdl2::TextureHandle::New(texture) : pu::sdl2::TextureHandle::Ref());
        }
        const auto current = std::find_if(this->themes.begin(), this->themes.end(), [](const theme::Preview &preview) { return preview.name == theme::Current(); });
        this->theme_selected = current != this->themes.end() ? static_cast<s32>(current - this->themes.begin()) : 0;
        this->theme_first = 0;
        this->focus = Focus::ThemePicker;
        this->picker_rise.Start();
        sound::Play(sound::Effect::Open);
        this->Rebuild();
    }

    void SettingsScreen::AddThemePicker() {
        constexpr s32 Visible = 4;
        constexpr double CardW = 224, ThumbH = 126, Gap = 24, Pad = 40;
        constexpr double W = Visible * CardW + (Visible - 1) * Gap + 2 * Pad, X = (DesignWidth - W) / 2, H = 340, Y = (DesignHeight - 60 - H) / 2;
        const s32 count = static_cast<s32>(this->themes.size());
        this->theme_first = std::clamp(this->theme_first, std::max(0, this->theme_selected - Visible + 1), this->theme_selected);

        AddPaint(*this, [this]() {
            draw::Rect(0, 0, DesignWidth, DesignHeight, color::WithAlpha(color::Overlay, static_cast<u8>(color::Overlay.a * this->picker_rise.Progress())));
            draw::Translate rise(0, (1.0 - this->picker_rise.Progress()) * 36);
            draw::RoundRect(X, Y, W, H, 30, color::Panel);
        });
        AddText(*this, X + Pad, Y + 32, strings::Get(Id::SettingTheme), 26, color::Text);

        for (s32 slot = 0; slot < Visible && this->theme_first + slot < count; ++slot) {
            const s32 index = this->theme_first + slot;
            const auto &preview = this->themes[index];
            const double x = X + Pad + slot * (CardW + Gap), y = Y + 92;
            const bool focused = index == this->theme_selected;
            AddPaint(*this, [this, index, x, y, focused]() {
                draw::Translate rise(0, (1.0 - this->picker_rise.Progress()) * 36);
                const auto &preview = this->themes[index];
                if (const auto &thumbnail = this->theme_thumbnails[index]; thumbnail != nullptr) {
                    draw::Image(thumbnail->Get(), x, y, CardW, ThumbH);
                } else {
                    /* No picture: a sketch of the shelf in the theme's colors. */
                    draw::RoundRect(x, y, CardW, ThumbH, 14, preview.ground);
                    draw::RoundRect(x + 10, y + 22, 50, 50, 8, preview.raised);
                    draw::RoundOutline(x + 7, y + 19, 56, 56, 10, 2.5, preview.primary);
                    for (s32 i = 0; i < 3; ++i) {
                        draw::RoundRect(x + 72 + i * 44, y + 38, 38, 34, 6, preview.panel);
                    }
                    draw::RoundRect(x + 10, y + 88, 90, 7, 3.5, preview.text);
                    draw::RoundRect(x + CardW - 64, y + 98, 52, 16, 8, preview.primary);
                }
                if (focused) {
                    draw::FocusRing(x, y, CardW, ThumbH, 14, 4);
                }
            });
            const std::string name = preview.name.empty() ? strings::Get(Id::SettingThemeDefault) : preview.display_name;
            AddText(*this, x, y + ThumbH + 14, name, 17, focused ? color::Text : color::TextSoft, Align::Left, CardW);
            if (preview.name == theme::Current()) {
                AddText(*this, x, y + ThumbH + 38, strings::Get(Id::ThemeInUse), 14, color::Primary, Align::Left, CardW);
            }
        }
        if (this->theme_first > 0) {
            AddPaint(*this, []() { draw::Chevron(X + 16, Y + 92 + ThumbH / 2, 18, color::Muted); });
        }
        if (this->theme_first + Visible < count) {
            AddPaint(*this, []() { draw::Chevron(X + W - 16, Y + 92 + ThumbH / 2, 18, color::Muted); });
        }
    }

    void SettingsScreen::OnThemePickerInput(u64 keys_down) {
        const s32 count = static_cast<s32>(this->themes.size());
        if (keys_down & HidNpadButton_B) {
            sound::Play(sound::Effect::Back);
            this->focus = Focus::Content;
            this->Rebuild();
        } else if ((keys_down & HidNpadButton_AnyLeft) && this->theme_selected > 0) {
            sound::Play(sound::Effect::Move);
            --this->theme_selected;
            this->Rebuild();
        } else if ((keys_down & HidNpadButton_AnyRight) && this->theme_selected < count - 1) {
            sound::Play(sound::Effect::Move);
            ++this->theme_selected;
            this->Rebuild();
        } else if (keys_down & HidNpadButton_A) {
            theme::Apply(this->themes[this->theme_selected].name);
            sound::Reload();
            sound::Play(sound::Effect::Select);
            this->Rebuild();
        }
    }

    void SettingsScreen::AddFooter() {
        if (this->focus == Focus::ThemePicker) {
            AddHintBar(*this, DesignWidth - 48, FooterCenterY, { { "B", strings::Get(Id::HintBack) }, { "A", strings::Get(Id::HintApply) } }, color::TextSoft);
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
        AddHintBar(*this, DesignWidth - 48, FooterCenterY, hints, color::Muted);
    }

    void SettingsScreen::SelectSection(Section section) {
        this->section_from_y = this->section_glide.Between(this->section_from_y, SectionY(static_cast<s32>(this->section)));
        this->section_glide.Start();
        this->content_fade.Start();
        this->section = section;
        this->selected = 0;
        this->toggled = -1;
        sound::Play(sound::Effect::Move);
        this->Rebuild();
    }

    void SettingsScreen::SelectCard(s32 index) {
        this->focus_from_y = this->focus_glide.Between(this->focus_from_y, CardY(this->selected));
        this->focus_glide.Start();
        this->selected = index;
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

    void SettingsScreen::OnInput(u64 keys_down) {
        if (this->focus == Focus::ThemePicker) {
            this->OnThemePickerInput(keys_down);
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
