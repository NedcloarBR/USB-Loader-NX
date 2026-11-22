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
#include <ui/library_screen.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sound.hpp>
#include <strings.hpp>

namespace ui {

    namespace {

        using strings::Id;

        constexpr double Edge = 48;
        constexpr double HeaderH = 80;
        constexpr double FooterY = DesignHeight - 60, FooterCenterY = FooterY + 30;

        /* The shelf: the selected cover large at the left, the next ones queued to its right on the same baseline. */
        struct Pose {
            double x, y, size;
        };
        constexpr double ShelfY = 104, BigCover = 280, SmallCover = 184, ShelfGap = 24;
        constexpr double ShelfBase = ShelfY + BigCover, ShelfLineY = ShelfBase + 7;
        constexpr double QueueX = Edge + BigCover + ShelfGap, QueueStep = SmallCover + ShelfGap;

        /* Slot 0 is the selected cover; slot k > 0 the k-th next one; slots < 0 are off screen to the left. */
        Pose SlotPose(s32 slot) {
            if (slot == 0) {
                return { Edge, ShelfY, BigCover };
            }
            /* Off screen to the left, with room to spare: an edge left in view would show as a sliver. */
            const double x = slot > 0 ? QueueX + (slot - 1) * QueueStep : -SmallCover - ShelfGap + (slot + 1) * QueueStep;
            return { x, ShelfBase - SmallCover, SmallCover };
        }

        Pose PoseAt(double position) {
            const s32 low = static_cast<s32>(std::floor(position));
            const double t = position - low;
            const Pose a = SlotPose(low), b = SlotPose(low + 1);
            return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.size + (b.size - a.size) * t };
        }

        /* Grid: 5 x 2 covers with their names under them. */
        constexpr s32 GridColumns = 5;
        constexpr s32 GridRows = 2;
        constexpr double GridCover = 200, GridGapX = 32, GridGapY = 24, GridY = 104, NameH = 40;
        constexpr double GridX = (DesignWidth - GridColumns * GridCover - (GridColumns - 1) * GridGapX) / 2;
        constexpr double GridRowH = GridCover + 12 + NameH;

        /* Windows rise this far (design px) as they open. */
        constexpr double WindowRise = 36;

        /* Placeholder covers draw the name at this size on a 240 px cover and scale it with the cover. */
        constexpr u32 PlaceholderFontSize = 20;
        constexpr double PlaceholderCover = 240;

        std::string SourceName(usbl::GameSource source) {
            switch (source) {
                case usbl::GameSource::Xci:       return strings::Get(Id::SourceXci);
                case usbl::GameSource::Installed: return strings::Get(Id::SourceInstalled);
                default:                          return strings::Get(Id::SourceNsp);
            }
        }

        std::string Fragmentation(const usbl::GameStatus &status) {
            if (status.fragments < 0) {
                return strings::Get(Id::HeavilyFragmented);
            }
            if (status.fragments > 1) {
                return strings::Format(Id::FragmentedParts, { { "PIECES", std::to_string(status.fragments) } });
            }
            return strings::Get(Id::Contiguous);
        }

        /* What needs attention; empty when nothing does. Unplugged and hidden are facts, the rest are alerts. `detailed`
         * explains a missing update in full (the game window has room for it). */
        std::pair<std::string, bool> GameAlert(const library::Game &game, bool detailed = false) {
            const auto &status = game.status;
            std::string alert;
            bool is_alert = false;
            const auto add = [&](const std::string &text, bool alerting) {
                is_alert = is_alert || alerting;
                alert += alert.empty() ? text : ", " + text;
            };
            /* Parts on another drive come first: moving them is what fixes a missing update too. */
            if (!status.is_connected) {
                add(strings::Get(Id::Disconnected), false);
            } else if (status.has_parts_elsewhere) {
                add(strings::Get(detailed ? Id::PartsElsewhereHelp : Id::PartsElsewhere), true);
            } else if (game.update_missing) {
                add(strings::Get(detailed ? Id::UpdateMissingHelp : Id::UpdateMissing), true);
            } else if (status.fragments != 1) {
                add(Fragmentation(status), true);
            }
            if (status.is_hidden) {
                add(strings::Get(Id::Hidden), false);
            }
            return { alert, is_alert };
        }

        double DriveUsage(const usbl::DriveStatus &drive) {
            if (drive.capacity == 0 || drive.free_space == 0) {
                return 0.0;
            }
            return static_cast<double>(drive.capacity - std::min(drive.capacity, drive.free_space)) / static_cast<double>(drive.capacity);
        }

        std::string DriveName(const std::vector<usbl::DriveStatus> &drives, const usbl::GameStatus &status) {
            for (const auto &drive : drives) {
                if (drive.drive == status.drive) {
                    return drive.name;
                }
            }
            return {};
        }

        double TextWidth(const std::string &text, u32 size) {
            return pu::ui::render::GetTextWidth(Font(size), text) / Scale;
        }

        std::string PlayTime(const library::PlayStats &play) {
            constexpr u64 NsPerMinute = 60'000'000'000ull;
            const u64 minutes = play.play_time_ns / NsPerMinute;
            if (play.launches == 0 && minutes == 0) {
                return strings::Get(Id::NeverPlayed);
            }
            if (minutes < 60) {
                return strings::Format(Id::PlayTimeMinutes, { { "MINUTES", std::to_string(minutes) } });
            }
            return strings::Format(Id::PlayTimeHours, { { "HOURS", std::to_string(minutes / 60) } });
        }

        /* In the console's time zone, in the language's order. */
        std::string FormatDate(u64 posix_time) {
            TimeCalendarTime calendar = {};
            TimeCalendarAdditionalInfo info = {};
            if (R_FAILED(timeToCalendarTimeWithMyRule(posix_time, &calendar, &info))) {
                return {};
            }
            const auto two = [](u32 value) { return (value < 10 ? "0" : "") + std::to_string(value); };
            return strings::Format(Id::DateFormat, { { "DAY", two(calendar.day) }, { "MONTH", two(calendar.month) }, { "YEAR", std::to_string(calendar.year) } });
        }

        s32 Wrap(s32 value, s32 count) {
            return (value % count + count) % count;
        }

        /* The header's chips. */
        constexpr double ChipH = 40, ChipY = (HeaderH - ChipH) / 2, ChipTextY = ChipY + 10;

        void SourceIcon(usbl::GameSource source, double cx, double cy, pu::ui::Color c) {
            switch (source) {
                case usbl::GameSource::Xci:       draw::GameCard(cx - 9, cy - 12, 18, 24, c, color::Panel); break;
                case usbl::GameSource::Installed: draw::IconGrid(cx, cy, 22, c);                            break;
                default:                          draw::IconPackage(cx, cy, 24, c);                         break;
            }
        }

    }

    LibraryScreen::LibraryScreen(Actions actions) : Layout(), actions(std::move(actions)), app_icon(library::LoadRoundedImage("romfs:/icon.jpg")) {
        this->AddRenderCallback([this]() { this->Animate(); });
    }

    void LibraryScreen::Show(std::vector<library::Game> games, std::vector<usbl::DriveStatus> drives, std::string message, Browse browse) {
        const u64 selected_id = this->GetSelectedGame() != nullptr ? this->GetSelectedGame()->status.application_id : 0;
        this->games = std::move(games);
        this->drives = std::move(drives);
        this->message = std::move(message);
        this->browse = std::move(browse);
        const auto still_there = std::find_if(this->games.begin(), this->games.end(), [selected_id](const library::Game &game) { return game.status.application_id == selected_id; });
        if (still_there != this->games.end()) {
            this->selected = static_cast<s32>(still_there - this->games.begin());
        }
        this->selected = std::clamp<s32>(this->selected, 0, std::max<s32>(0, static_cast<s32>(this->games.size()) - 1));
        if (this->window == Window::Game && this->games.empty()) {
            this->window = Window::None;
        }

        this->name_textures.clear();
        for (const auto &game : this->games) {
            pu::sdl2::TextureHandle::Ref texture;
            if (game.icon == nullptr) {
                const std::string name = Fit(game.name, PlaceholderFontSize, PlaceholderCover * 0.84);
                texture = pu::sdl2::TextureHandle::New(pu::ui::render::RenderText(Font(PlaceholderFontSize), name, color::Text));
            }
            this->name_textures.push_back(texture);
        }
        this->Rebuild();
    }

    const library::Game *LibraryScreen::GetSelectedGame() const {
        return this->games.empty() ? nullptr : &this->games[this->selected];
    }

    void LibraryScreen::Rebuild() {
        /* Plutonium containers cannot remove single elements: the screen is rebuilt on every change. */
        this->Clear();
        this->SetBackgroundColor(color::Ground);
        this->sliding.clear();
        AddPaint(*this, []() { draw::Background(); });

        this->AddHeader();
        if (this->games.empty()) {
            const std::string text = this->message.empty() ? strings::Get(Id::NoGames) : this->message;
            AddText(*this, DesignWidth / 2, 330, text, 21, color::MutedSoft, Align::Center, 1100);
        } else if (this->view == View::Carousel) {
            this->AddCarousel();
        } else {
            this->AddGrid();
        }
        if (this->window == Window::None) {
            this->AddFooter();
        }

        if (this->window == Window::Game && this->GetSelectedGame() != nullptr) {
            this->AddGameWindow(*this->GetSelectedGame());
        } else if (this->window == Window::Drives) {
            this->AddDrivesWindow();
        } else if (this->window == Window::Message) {
            if (this->message_return == Window::Game && this->GetSelectedGame() != nullptr) {
                this->AddGameWindow(*this->GetSelectedGame());
            }
            this->AddMessageWindow();
        }

        /* Coming from another screen: everything fades in from the ground. */
        AddPaint(*this, [this]() {
            if (const double progress = this->enter.Progress(); progress < 1.0) {
                draw::Rect(0, 0, DesignWidth, DesignHeight, color::WithAlpha(color::Ground, static_cast<u8>((1.0 - progress) * 255)));
            }
        });
    }

    void LibraryScreen::Enter() {
        this->enter.Start();
        /* The first time, the covers also glide in from the right. */
        if (!this->has_entered) {
            this->has_entered = true;
            this->slide_from = 3;
            this->slide.Start();
        }
    }

    void LibraryScreen::PaintCover(size_t index, double x, double y, double size, double opacity) {
        const auto &game = this->games[index];
        const double radius = size * IconCornerRatio;
        /* Fading is real transparency: a veil of the ground color would show the cover through a theme's picture. */
        const u8 alpha = static_cast<u8>(std::clamp(opacity, 0.0, 1.0) * 255);
        if (alpha == 0) {
            return;
        }
        const auto faded = [alpha](pu::ui::Color c) { return color::WithAlpha(c, static_cast<u8>(c.a * alpha / 255)); };
        if (game.icon != nullptr) {
            draw::Image(game.icon->Get(), x, y, size, size, alpha);
        } else {
            draw::RoundRect(x, y, size, size, radius, faded(color::Raised));
            if (const auto &name = this->name_textures[index]; name != nullptr) {
                s32 width = 0, height = 0;
                SDL_QueryTexture(name->Get(), nullptr, nullptr, &width, &height);
                const double k = size / PlaceholderCover / Scale;
                draw::Image(name->Get(), x + size * 0.08, y + size * 0.92 - height * k, width * k, height * k, alpha);
            }
        }

        if (game.is_favorite) {
            const double r = std::max(9.0, size * 0.064), cx = x + size - r - size * 0.057, cy = y + r + size * 0.057;
            draw::Circle(cx, cy, r, faded({ 0x0A, 0x0D, 0x13, 0x8C }));
            draw::Star(cx, cy, r * 0.58, faded(color::Star));
        }

        /* Games whose drive is unplugged are darker. */
        if (!game.status.is_connected) {
            draw::RoundRect(x, y, size, size, radius, faded(color::WithAlpha(color::Ground, 0x80)));
        }
    }

    void LibraryScreen::AddHeader() {
        AddPaint(*this, [this]() {
            if (this->app_icon != nullptr) {
                draw::Image(this->app_icon->Get(), Edge, 20, 40, 40);
            }
        });
        AddText(*this, Edge + 52, 27, strings::Get(Id::AppTitle), 21, color::Text);

        /* Right to left: the drives, the search (ZL, or the query with B to clear it), the sort order (−). */
        u64 free_space = 0, capacity = 0;
        for (const auto &drive : this->drives) {
            if (drive.free_space != 0) {
                free_space += drive.free_space;
                capacity += drive.capacity;
            }
        }
        const double usage = capacity != 0 ? static_cast<double>(capacity - free_space) / static_cast<double>(capacity) : 0.0;
        const std::string drives = this->drives.empty() ? strings::Get(Id::NoDrives)
                                 : capacity == 0      ? strings::Count(this->drives.size(), Id::DriveOne, Id::DriveMany)
                                                      : strings::Format(Id::DrivesFree, { { "DRIVES", strings::Count(this->drives.size(), Id::DriveOne, Id::DriveMany) }, { "FREE", strings::Size(free_space) } });
        const bool has_drives = !this->drives.empty();
        const double drives_w = TextWidth(drives, 15) + 50, drives_x = DesignWidth - Edge - drives_w;
        AddPaint(*this, [drives_x, drives_w, usage, has_drives]() {
            draw::RoundRect(drives_x, ChipY, drives_w, ChipH, ChipH / 2, color::Panel);
            draw::RoundOutline(drives_x, ChipY, drives_w, ChipH, ChipH / 2, 1, color::Track);
            draw::Circle(drives_x + 20, ChipY + ChipH / 2, 4, has_drives ? draw::UsageColor(usage) : color::Faint);
        });
        AddText(*this, drives_x + 34, ChipTextY, drives, 15, color::TextSoft);

        constexpr double MaxChipText = 300;
        const bool searching = !this->browse.query.empty();
        const std::string search = Fit(searching ? "“" + this->browse.query + "”" : strings::Get(Id::Search), 15, MaxChipText);
        const std::string search_button = searching ? "B" : "ZL";
        const double search_w = TextWidth(search, 15) + 60 + (searching ? 0 : 10), search_x = drives_x - 10 - search_w;
        const std::string sort = Fit(this->browse.sort_name, 15, MaxChipText);
        const double sort_w = TextWidth(sort, 15) + 60, sort_x = search_x - 10 - sort_w;
        AddPaint(*this, [search_x, search_w, sort_x, sort_w, searching]() {
            draw::RoundRect(sort_x, ChipY, sort_w, ChipH, ChipH / 2, color::Panel);
            draw::RoundRect(search_x, ChipY, search_w, ChipH, ChipH / 2, color::Panel);
            if (searching) {
                draw::RoundOutline(search_x, ChipY, search_w, ChipH, ChipH / 2, 1.5, color::Primary);
            }
        });
        AddButtonHint(*this, sort_x + 21, HeaderH / 2, "X");
        AddText(*this, sort_x + 44, ChipTextY, sort, 15, color::TextSoft);
        const double search_glyph = searching ? 24 : TextWidth("ZL", 13) + 14;
        AddButtonHint(*this, search_x + 8 + search_glyph / 2, HeaderH / 2, search_button);
        AddText(*this, search_x + 18 + search_glyph, ChipTextY, search, 15, searching ? color::Text : color::TextSoft);
    }

    void LibraryScreen::AddCarousel() {
        const s32 count = static_cast<s32>(this->games.size());
        /* Each game shows once: the selected one, then up to five after it, and the one before it off screen. */
        const s32 ahead = std::min(count - 1, 5);
        const s32 behind = count > 1 ? 1 : 0;

        AddPaint(*this, []() { draw::Rect(0, ShelfLineY, DesignWidth, 2, color::Track); });
        AddPaint(*this, [this, count, ahead, behind]() {
            const double shift = this->slide.Between(this->slide_from, 0.0);
            const double bob = Float(4) * this->slide.Progress();
            for (s32 offset = -behind - 1; offset <= ahead + 1; ++offset) {
                const double position = offset + shift;
                if (offset < -behind || offset > ahead || position < -1.5 || position > 6.0) {
                    continue;
                }
                const Pose pose = PoseAt(position);
                if (pose.x + pose.size < 0 || pose.x > DesignWidth) {
                    continue;
                }
                const double lift = std::abs(position) < 1.0 ? bob * (1.0 - std::abs(position)) : 0.0;
                /* The cover leaving the shelf (or coming back) is gone within the first third of the way off. */
                const double fade = position < 0 ? std::clamp(1.0 + position * 3, 0.0, 1.0) : 1.0;
                if (fade <= 0.0) {
                    continue;
                }
                this->PaintCover(Wrap(this->selected + offset, count), pose.x, pose.y + lift, pose.size, fade);
            }
            if (this->focus == Focus::Shelf) {
                draw::FocusRing(Edge, ShelfY + bob, BigCover, BigCover, BigCover * IconCornerRatio);
            }
        });

        /* Over a theme's picture, text sits on a veil of the ground color. */
        if (theme::BackgroundImage() != nullptr) {
            const double names_w = std::min(ahead, 4) * QueueStep - ShelfGap;
            AddPaint(*this, [names_w]() {
                if (names_w > 0) {
                    draw::RoundRect(QueueX - 8, ShelfLineY + 5, names_w + 16, 28, 10, color::WithAlpha(color::Ground, 0xC0));
                }
                draw::RoundRect(Edge - 20, 424, 840, 220, 24, color::WithAlpha(color::Ground, 0xC0));
            });
        }

        /* The names under the queued covers fade in once they settle. */
        for (s32 slot = 1; slot <= std::min(ahead, 4); ++slot) {
            const auto &game = this->games[Wrap(this->selected + slot, count)];
            const Pose pose = SlotPose(slot);
            AddText(*this, pose.x, ShelfLineY + 9, game.name, 15, color::Muted, Align::Left, SmallCover);
        }

        const auto &game = this->games[this->selected];
        const auto &status = game.status;
        constexpr double InfoY = 436, InfoW = 820;
        AddText(*this, Edge, InfoY, status.is_connected ? strings::Format(Id::OnDrive, { { "DRIVE", DriveName(this->drives, status) } }) : strings::Get(Id::Disconnected), 15, color::Muted, Align::Left, InfoW);
        AddText(*this, Edge - 2, InfoY + 22, game.name, 46, color::Text, Align::Left, InfoW);

        std::vector<std::string> chips = { SourceName(status.source) };
        if (!game.display_version.empty()) {
            chips.push_back(strings::Format(Id::Version, { { "VERSION", game.display_version } }));
        }
        if (status.add_on_count > 0) {
            chips.push_back(strings::Count(status.add_on_count, Id::AddOnOne, Id::AddOnMany));
        }
        chips.push_back(strings::Size(status.size));
        std::vector<std::pair<double, double>> chip_boxes;
        double chip_x = Edge;
        for (const auto &chip : chips) {
            const double w = TextWidth(chip, 14) + 24;
            chip_boxes.push_back({ chip_x, w });
            chip_x += w + 8;
        }
        AddPaint(*this, [chip_boxes]() {
            for (const auto &[x, w] : chip_boxes) {
                draw::RoundRect(x, InfoY + 84, w, 30, 8, color::Panel);
            }
        });
        for (size_t i = 0; i < chips.size(); ++i) {
            AddText(*this, chip_boxes[i].first + 12, InfoY + 90, chips[i], 14, color::TextSoft);
        }

        constexpr double StatsY = InfoY + 132;
        AddPaint(*this, []() { draw::IconClock(Edge + 9, StatsY + 10, 18, color::Muted); });
        const auto play = AddText(*this, Edge + 26, StatsY, PlayTime(game.play), 15, color::Muted);
        if (game.play.last_played != 0) {
            AddText(*this, Edge + 26 + play->GetWidth() / Scale + 28, StatsY, strings::Format(Id::LastPlayed, { { "DATE", FormatDate(game.play.last_played) } }), 15, color::Muted);
        }
        if (const auto [alert, is_alert] = GameAlert(game); !alert.empty()) {
            const pu::ui::Color tint = is_alert ? color::Alert : color::Muted;
            AddPaint(*this, [tint]() { draw::IconWarning(Edge + 9, StatsY + 38, 18, tint); });
            AddText(*this, Edge + 26, StatsY + 28, alert, 15, tint, Align::Left, InfoW);
        }

        /* Down moves the focus from the shelf to Launch and Details; A on the shelf launches. The button A would press
         * is the filled one with the A. */
        const std::string launch = strings::Get(Id::Launch), details = strings::Get(Id::Details);
        constexpr double ButtonY = 572, ButtonH = 64;
        const double launch_w = TextWidth(launch, 20) + 96, details_w = TextWidth(details, 20) + 96;
        const double details_x = DesignWidth - Edge - details_w, launch_x = details_x - 14 - launch_w;
        const bool on_buttons = this->focus == Focus::Buttons;
        const bool details_active = on_buttons && this->home_button == HomeButton::Details;
        this->AddActionButton(launch_x, ButtonY, launch_w, ButtonH, launch, !details_active, status.is_connected, on_buttons && !details_active, false);
        this->AddActionButton(details_x, ButtonY, details_w, ButtonH, details, details_active, true, details_active, false);

        /* The new name and details fade in as the covers settle. */
        AddPaint(*this, [this]() {
            if (const double progress = this->slide.Progress(); progress < 1.0) {
                const auto veil = color::WithAlpha(color::Ground, static_cast<u8>((1.0 - progress) * 255));
                draw::Rect(Edge - 8, InfoY - 4, InfoW + 16, FooterY - InfoY, veil);
                draw::Rect(QueueX, ShelfLineY + 4, DesignWidth - QueueX, 30, veil);
            }
        });
    }

    void LibraryScreen::AddGrid() {
        const s32 count = static_cast<s32>(this->games.size());
        const s32 row = this->selected / GridColumns;
        if (row < this->first_row) {
            this->first_row = row;
        } else if (row >= this->first_row + GridRows) {
            this->first_row = row - GridRows + 1;
        }

        for (s32 cell = 0; cell < GridColumns * GridRows; ++cell) {
            const s32 index = this->first_row * GridColumns + cell;
            if (index >= count) {
                break;
            }
            const double x = GridX + (cell % GridColumns) * (GridCover + GridGapX);
            const double y = GridY + static_cast<double>(cell / GridColumns) * (GridRowH + GridGapY);
            const bool is_selected = index == this->selected;
            const auto &game = this->games[index];
            const bool unplugged = !game.status.is_connected;
            AddPaint(*this, [this, index, x, y, is_selected, unplugged]() {
                const double lift = is_selected ? Float(2) * this->focus_glide.Progress() : 0.0;
                this->PaintCover(index, x, y + lift, GridCover, 1.0);
            });
            if (unplugged || game.update_missing || game.status.has_parts_elsewhere) {
                const std::string label = strings::Get(unplugged ? Id::Disconnected : (game.status.has_parts_elsewhere ? Id::PartsElsewhereBadge : Id::UpdateMissingBadge));
                const double w = std::min(GridCover - 24, TextWidth(label, 13) + 20);
                AddPaint(*this, [x, y, w]() { draw::RoundRect(x + 12, y + 12, w, 26, 13, { 0x0A, 0x0D, 0x13, 0xB4 }); });
                AddText(*this, x + 22, y + 16, label, 13, unplugged ? color::Text : color::Alert, Align::Left, w - 20);
            }
            const auto lines = WrapLines(game.name, 15, GridCover, 2);
            if (theme::BackgroundImage() != nullptr) {
                const double names_h = lines.size() * 20 + 10;
                AddPaint(*this, [x, y, names_h]() { draw::RoundRect(x - 6, y + GridCover + 7, GridCover + 12, names_h, 10, color::WithAlpha(color::Ground, 0xC0)); });
            }
            for (size_t line = 0; line < lines.size(); ++line) {
                AddText(*this, x, y + GridCover + 12 + line * 20, lines[line], 15, is_selected ? color::Text : color::Muted);
            }
        }

        const s32 cell = this->selected - this->first_row * GridColumns;
        this->focus_x = GridX + (cell % GridColumns) * (GridCover + GridGapX);
        this->focus_y = GridY + static_cast<double>(cell / GridColumns) * (GridRowH + GridGapY);
        AddPaint(*this, [this]() {
            const double x = this->focus_glide.Between(this->focus_from_x, this->focus_x);
            const double y = this->focus_glide.Between(this->focus_from_y, this->focus_y) + Float(2) * this->focus_glide.Progress();
            draw::FocusRing(x, y, GridCover, GridCover, GridCover * IconCornerRatio);
        });
    }

    void LibraryScreen::AddFooter() {
        AddPaint(*this, []() {
            draw::Rect(0, FooterY - 1, DesignWidth, 60 + 1, color::Ground);
            draw::Rect(0, FooterY - 1, DesignWidth, 1, color::Track);
        });
        if (!this->games.empty()) {
            AddText(*this, Edge, FooterCenterY - 10, strings::Format(Id::GameOf, { { "INDEX", std::to_string(this->selected + 1) }, { "COUNT", std::to_string(this->games.size()) } }), 14, color::Muted);
        }

        const bool carousel = this->view == View::Carousel;
        std::vector<Hint> hints = { { "-", strings::Get(Id::HintExit) }, { "+", strings::Get(Id::HintSettings) } };
        hints.push_back({ "R", strings::Get(carousel ? Id::HintGrid : Id::HintCarousel) });
        hints.push_back({ "Y", strings::Get(Id::HintDrives) });
        if (!this->browse.query.empty()) {
            hints.push_back({ "B", strings::Get(Id::HintClearSearch) });
        }
        if (!this->games.empty()) {
            hints.push_back({ "A", strings::Get(carousel && this->focus == Focus::Shelf ? Id::Launch : (carousel ? Id::HintSelect : Id::Details)) });
        }
        AddHintBar(*this, DesignWidth - Edge, FooterCenterY, hints, color::Muted);
    }

    void LibraryScreen::AddActionButton(double x, double y, double w, double h, const std::string &label, bool active, bool enabled, bool ring, bool in_window) {
        AddPaint(*this, [this, x, y, w, h, active, enabled, ring, in_window]() {
            draw::Translate rise(0, in_window ? this->WindowOffset() : 0.0);
            if (active) {
                draw::RoundRect(x, y, w, h, h / 2, enabled ? color::Primary : color::Raised);
                draw::Circle(x + h / 2, y + h / 2, h * 0.31, enabled ? color::OnPrimary : color::Track);
            } else {
                draw::RoundRect(x, y, w, h, h / 2, color::Panel);
                draw::RoundOutline(x, y, w, h, h / 2, 1.5, color::Track);
            }
            if (ring) {
                draw::FocusRing(x, y, w, h, h / 2, 4);
            }
        });
        const auto add = [this, in_window](pu::ui::elm::TextBlock::Ref text) {
            if (in_window) {
                this->Slide(text);
            }
        };
        const double text_y = y + (h - 26) / 2;
        if (active) {
            add(AddText(*this, x + h / 2, text_y + 2, "A", 18, enabled ? color::Primary : color::Faint, Align::Center));
            add(AddText(*this, x + h + 2, text_y, label, 20, enabled ? color::OnPrimary : color::Faint, Align::Left, w - h - 24));
        } else {
            add(AddText(*this, x + w / 2, text_y, label, 20, color::Text, Align::Center, w - 32));
        }
    }

    double LibraryScreen::WindowOffset() const {
        return (1.0 - this->window_rise.Progress()) * WindowRise;
    }

    pu::ui::elm::TextBlock::Ref LibraryScreen::Slide(pu::ui::elm::TextBlock::Ref text) {
        this->sliding.push_back({ [text](s32 y) { text->SetY(y); }, text->GetY() });
        return text;
    }

    void LibraryScreen::AddWindowPanel(double x, double y, double w, double h) {
        AddPaint(*this, [this, x, y, w, h]() {
            draw::Rect(0, 0, DesignWidth, DesignHeight, color::WithAlpha(color::Overlay, static_cast<u8>(color::Overlay.a * this->window_rise.Progress())));
            draw::Translate rise(0, this->WindowOffset());
            draw::RoundRect(x, y, w, h, 30, color::Panel);
        });
    }

    void LibraryScreen::AddGameWindow(const library::Game &game) {
        constexpr double X = 160, Y = 72, W = 960, H = 516, Pad = 40;
        constexpr double CoverSize = 300, CoverX = X + Pad, CoverY = Y + Pad;
        constexpr double ColumnX = CoverX + CoverSize + 44, ColumnW = X + W - Pad - ColumnX, ValueX = ColumnX + 170;
        const auto &status = game.status;
        this->AddWindowPanel(X, Y, W, H);

        /* Left: the cover, and what it is and where. */
        const s32 index = this->selected;
        const auto source = status.source;
        AddPaint(*this, [this, index, source]() {
            draw::Translate rise(0, this->WindowOffset());
            const double k = 0.94 + 0.06 * this->window_rise.Progress();
            const double size = CoverSize * k, offset = (CoverSize - size) / 2;
            this->PaintCover(index, CoverX + offset, CoverY + offset + Float(3), size, 1.0);
            SourceIcon(source, CoverX + 14, CoverY + CoverSize + 38, color::TextSoft);
        });
        this->Slide(AddText(*this, CoverX + 36, CoverY + CoverSize + 18, SourceName(source), 15, color::TextSoft, Align::Left, CoverSize - 36));
        const std::string where = status.is_connected ? strings::Format(Id::OnDrive, { { "DRIVE", DriveName(this->drives, status) } }) : strings::Get(Id::PlugInDrive);
        this->Slide(AddText(*this, CoverX + 36, CoverY + CoverSize + 40, where, 15, status.is_connected ? color::Muted : color::Pending, Align::Left, CoverSize - 36));

        /* Right: the name with its favorite star (Y), the details, what needs attention and the two actions. */
        double title_end = 0;
        double y = CoverY;
        if (Fit(game.name, 40, ColumnW - 48) == game.name) {
            const auto title = this->Slide(AddText(*this, ColumnX, y - 4, game.name, 40, color::Text));
            title_end = title->GetWidth() / Scale;
            y += 52;
        } else {
            const auto lines = WrapLines(game.name, 30, ColumnW - 48, 2);
            for (size_t line = 0; line < lines.size(); ++line) {
                const auto text = this->Slide(AddText(*this, ColumnX, y + line * 38, lines[line], 30, color::Text));
                title_end = text->GetWidth() / Scale;
            }
            y += 38 * lines.size() + 10;
        }
        const double star_x = ColumnX + title_end + 28, star_y = y - (y - CoverY) / 2 - 4;
        const bool favorite = game.is_favorite;
        AddPaint(*this, [this, star_x, star_y, favorite]() {
            draw::Translate rise(0, this->WindowOffset());
            draw::Star(star_x, star_y, 15, favorite ? color::Star : color::Track);
        });

        std::vector<std::pair<Id, std::string>> rows;
        rows.push_back({ Id::RowVersion, game.display_version.empty() ? "—" : game.display_version });
        const Id update_state = status.has_update ? Id::UpdateInstalled : (status.has_parts_elsewhere ? Id::UpdateElsewhereRow : (game.update_missing ? Id::UpdateMissingRow : Id::UpdateNone));
        rows.push_back({ Id::RowUpdate, strings::Get(update_state) });
        rows.push_back({ Id::RowAddOns, std::to_string(status.add_on_count) });
        rows.push_back({ Id::RowSize, strings::Size(status.size) });
        if (status.is_connected) {
            rows.push_back({ Id::RowFile, Fragmentation(status) });
        }
        rows.push_back({ Id::RowPlayTime, PlayTime(game.play) });
        if (game.play.last_played != 0) {
            rows.push_back({ Id::RowLastPlayed, FormatDate(game.play.last_played) });
        }
        y += 14;
        for (const auto &[label, value] : rows) {
            this->Slide(AddText(*this, ColumnX, y, strings::Get(label), 16, color::Muted, Align::Left, 160));
            const bool warn = (label == Id::RowFile && status.fragments != 1) || (label == Id::RowUpdate && !status.has_update && (game.update_missing || status.has_parts_elsewhere));
            this->Slide(AddText(*this, ValueX, y, value, 16, warn ? color::Alert : color::Text, Align::Left, ColumnW - 170));
            y += 29;
        }

        if (const auto [alert, is_alert] = GameAlert(game, true); !alert.empty()) {
            const auto lines = WrapLines(alert, 14, ColumnW - 60, 3);
            const double box_y = y + 6, box_h = 24 + lines.size() * 20;
            const pu::ui::Color surface = is_alert ? color::AlertSurface : color::Raised;
            const pu::ui::Color icon = is_alert ? color::Alert : color::Muted;
            AddPaint(*this, [this, box_y, box_h, surface, icon]() {
                draw::Translate rise(0, this->WindowOffset());
                draw::RoundRect(ColumnX, box_y, ColumnW, box_h, 14, surface);
                draw::IconWarning(ColumnX + 25, box_y + 22, 18, icon);
            });
            for (size_t line = 0; line < lines.size(); ++line) {
                this->Slide(AddText(*this, ColumnX + 44, box_y + 12 + line * 20, lines[line], 14, is_alert ? color::AlertText : color::TextSoft));
            }
        }

        /* The two actions; the focused one is filled, carries the A and the focus ring. */
        constexpr double ButtonY = Y + H - Pad - 60, ButtonH = 60;
        const std::string launch = strings::Get(Id::Launch);
        const std::string hide = strings::Get(status.is_hidden ? Id::ShowOnHome : Id::HideFromHome);
        const double launch_w = TextWidth(launch, 20) + 96, hide_x = ColumnX + launch_w + 14, hide_w = std::min(ColumnW - launch_w - 14, TextWidth(hide, 20) + 96);
        const bool focus_launch = this->game_button == GameButton::Launch;
        this->AddActionButton(ColumnX, ButtonY, launch_w, ButtonH, launch, focus_launch, status.is_connected, focus_launch, true);
        this->AddActionButton(hide_x, ButtonY, hide_w, ButtonH, hide, !focus_launch, true, !focus_launch, true);

        std::vector<Hint> hints = { { "B", strings::Get(Id::HintBack) } };
        if (this->games.size() > 1) {
            hints.push_back({ "L R", strings::Get(Id::HintSwitchGame) });
        }
        hints.push_back({ "Y", strings::Get(game.is_favorite ? Id::HintUnfavorite : Id::HintFavorite) });
        AddHintBar(*this, DesignWidth - Edge, FooterCenterY, hints, color::TextSoft);
    }

    void LibraryScreen::AddDrivesWindow() {
        constexpr double X = 200, W = 880, Pad = 40, CardX = X + Pad, CardW = W - 2 * Pad, CardGap = 12;
        const auto card_height = [](const usbl::DriveStatus &drive) { return DriveUsage(drive) >= 0.9 ? 140.0 : 114.0; };
        double cards_h = 0;
        for (const auto &drive : this->drives) {
            cards_h += card_height(drive) + CardGap;
        }
        const double h = 36 + 100 + std::max(cards_h - CardGap, 60.0) + Pad;
        const double y = std::max(24.0, (FooterY - h) / 2);
        this->AddWindowPanel(X, y, W, h);

        this->Slide(AddText(*this, CardX, y + 30, strings::Get(Id::DevicesTitle), 30, color::Text));
        u64 free_space = 0, capacity = 0, games = 0;
        for (const auto &drive : this->drives) {
            if (drive.free_space != 0) {
                free_space += drive.free_space;
                capacity += drive.capacity;
            }
            games += drive.game_count;
        }
        if (capacity != 0) {
            const std::string total = strings::Format(Id::Total, { { "FREE", strings::Size(free_space) }, { "TOTAL", strings::Size(capacity) }, { "GAMES", strings::Count(games, Id::GameOne, Id::GameMany) } });
            this->Slide(AddText(*this, CardX, y + 76, total, 15, color::Muted, Align::Left, CardW));
        }

        double card_y = y + 136;
        if (this->drives.empty()) {
            this->Slide(AddText(*this, X + W / 2, card_y + 18, strings::Get(Id::NoDrives), 19, color::MutedSoft, Align::Center, CardW));
        }
        for (const auto &drive : this->drives) {
            const double usage = DriveUsage(drive), card_h = card_height(drive);
            const bool almost_full = usage >= 0.9;
            const bool has_space = drive.free_space != 0;
            constexpr double BarX = CardX + 24, PercentW = 56;
            constexpr double BarW = CardW - 48 - PercentW;
            AddPaint(*this, [this, card_y, card_h, usage, has_space]() {
                draw::Translate rise(0, this->WindowOffset());
                draw::RoundRect(CardX, card_y, CardW, card_h, 20, color::Raised);
                draw::RoundRect(CardX + 24, card_y + 20, 48, 48, 14, color::Ground);
                draw::IconUsbStick(CardX + 48, card_y + 44, 26, color::TextSoft);
                if (has_space) {
                    draw::UsageBar(BarX, card_y + 84, BarW, 12, usage);
                }
            });
            const std::string space = has_space ? strings::Format(Id::FreeOf, { { "FREE", strings::Size(drive.free_space) }, { "TOTAL", strings::Size(drive.capacity) } }) : std::string();
            const double space_w = space.empty() ? 0 : TextWidth(space, 17) + 24;
            this->Slide(AddText(*this, CardX + 88, card_y + 20, drive.name, 19, color::Text, Align::Left, CardW - 112 - space_w));
            if (!space.empty()) {
                this->Slide(AddText(*this, CardX + CardW - 24, card_y + 22, space, 17, color::Text, Align::Right));
                this->Slide(AddText(*this, CardX + CardW - 24, card_y + 79, std::to_string(static_cast<s32>(std::lround(usage * 100))) + "%", 14, almost_full ? color::ErrorText : color::Text, Align::Right));
            }

            std::string detail;
            switch (drive.state) {
                case usbl::DriveState::Ready:
                    detail = (drive.is_exfat ? "exFAT, " : "FAT32, ") + strings::Count(drive.game_count, Id::GameOne, Id::GameMany) + ", " +
                             strings::Count(drive.update_count, Id::UpdateOne, Id::UpdateMany) + ", " + strings::Count(drive.add_on_count, Id::AddOnOne, Id::AddOnMany);
                    break;
                case usbl::DriveState::NoGamesFolder: detail = strings::Get(Id::DriveNoGamesFolder); break;
                default:                              detail = strings::Get(Id::DriveUnreadable);    break;
            }
            /* Where homebrew read it (protocol 1.1; 0 from an older module). */
            if (drive.volume_serial != 0) {
                char path[48];
                std::snprintf(path, sizeof(path), "sdmc:/usbloadernx/usb/%04X-%04X/", static_cast<unsigned>(drive.volume_serial >> 16), static_cast<unsigned>(drive.volume_serial & 0xFFFF));
                detail += ", " + std::string(path);
            }
            this->Slide(AddText(*this, CardX + 88, card_y + 47, detail, 14, color::Muted, Align::Left, CardW - 112));
            if (almost_full) {
                AddPaint(*this, [this, card_y]() {
                    draw::Translate rise(0, this->WindowOffset());
                    draw::IconWarning(CardX + 32, card_y + 118, 16, color::ErrorText);
                });
                this->Slide(AddText(*this, CardX + 48, card_y + 108, strings::Get(Id::AlmostFull), 14, color::ErrorText, Align::Left, CardW - 72));
            }
            card_y += card_h + CardGap;
        }

        AddHintBar(*this, DesignWidth - Edge, FooterCenterY, { { "B", strings::Get(Id::HintBack) } }, color::TextSoft);
    }

    void LibraryScreen::ShowMessage(const std::string &title, const std::string &text, bool with_game_icon, MessageAction action) {
        this->message_title = title;
        this->message_text = text;
        this->message_action = std::move(action);
        this->message_on_action = true;
        this->message_has_icon = with_game_icon && this->GetSelectedGame() != nullptr;
        this->message_return = this->window == Window::Message ? this->message_return : this->window;
        this->window = Window::Message;
        this->window_rise.Start();
        this->Rebuild();
    }

    void LibraryScreen::AddMessageWindow() {
        constexpr double W = 720, X = (DesignWidth - W) / 2, Pad = 40, IconSize = 88;
        const double text_x = X + Pad + (this->message_has_icon ? IconSize + 24 : 0), text_w = X + W - Pad - text_x;
        const auto title = WrapLines(this->message_title, 26, text_w, 2);
        const auto lines = WrapLines(this->message_text, 16, text_w, 11);
        const double title_h = title.size() * 34, body_h = lines.size() * 24;
        const double content_h = std::max(this->message_has_icon ? IconSize : 0.0, title_h + 14 + body_h);
        const double h = Pad + content_h + 32 + 56 + Pad, y = std::max(24.0, (FooterY - h) / 2);
        this->AddWindowPanel(X, y, W, h);

        if (this->message_has_icon) {
            const s32 index = this->selected;
            AddPaint(*this, [this, index, y]() {
                draw::Translate rise(0, this->WindowOffset());
                this->PaintCover(index, X + Pad, y + Pad, IconSize, 1.0);
            });
        }
        for (size_t i = 0; i < title.size(); ++i) {
            this->Slide(AddText(*this, text_x, y + Pad - 2 + i * 34, title[i], 26, color::Text));
        }
        for (size_t i = 0; i < lines.size(); ++i) {
            this->Slide(AddText(*this, text_x, y + Pad + title_h + 14 + i * 24, lines[i], 16, color::TextSoft));
        }

        if (!this->message_action.label.empty()) {
            const std::string close = strings::Get(Id::Close), action = this->message_action.label;
            const double action_w = TextWidth(action, 20) + 96, close_w = TextWidth(close, 20) + 64, button_y = y + h - Pad - 56;
            const double action_x = X + W - Pad - action_w, close_x = action_x - 16 - close_w;
            const bool on_action = this->message_on_action;
            this->AddActionButton(close_x, button_y, close_w, 56, close, !on_action, true, !on_action, true);
            this->AddActionButton(action_x, button_y, action_w, 56, action, on_action, true, on_action, true);
            return;
        }

        const std::string ok = strings::Get(Id::Ok);
        const double button_w = TextWidth(ok, 18) + 88, button_x = X + W - Pad - button_w, button_y = y + h - Pad - 56;
        AddPaint(*this, [this, button_x, button_w, button_y]() {
            draw::Translate rise(0, this->WindowOffset());
            draw::RoundRect(button_x, button_y, button_w, 56, 28, color::Primary);
            draw::Circle(button_x + 28, button_y + 28, 17, color::OnPrimary);
            draw::FocusRing(button_x, button_y, button_w, 56, 28, 4);
        });
        this->Slide(AddText(*this, button_x + 28, button_y + 17, "A", 16, color::Primary, Align::Center));
        this->Slide(AddText(*this, button_x + 56, button_y + 16, ok, 18, color::OnPrimary));
    }

    void LibraryScreen::Animate() {
        if (!this->sliding.empty()) {
            const s32 offset = S(this->WindowOffset());
            for (const auto &element : this->sliding) {
                element.set_y(element.base_y + offset);
            }
        }
    }

    void LibraryScreen::MoveFocus(Focus focus) {
        sound::Play(sound::Effect::Move);
        this->focus = focus;
        this->Rebuild();
    }

    void LibraryScreen::LaunchSelected() {
        const auto *game = this->GetSelectedGame();
        if (game->status.is_connected) {
            this->actions.launch(*game);
        } else {
            sound::Play(sound::Effect::Error);
        }
    }

    void LibraryScreen::Move(s32 delta) {
        const s32 count = static_cast<s32>(this->games.size());
        if (count == 0) {
            return;
        }
        if (this->view == View::Carousel) {
            if (count == 1) {
                return;
            }
            /* Continue from wherever a running slide is, so holding the stick keeps the covers moving. */
            this->slide_from = std::clamp(this->slide.Between(this->slide_from, 0.0) + delta, -3.0, 3.0);
            this->slide.Start();
            this->selected = Wrap(this->selected + delta, count);
        } else {
            const s32 target = this->selected + delta;
            if (target < 0 || target >= count) {
                return;
            }
            this->focus_from_x = this->focus_glide.Between(this->focus_from_x, this->focus_x);
            this->focus_from_y = this->focus_glide.Between(this->focus_from_y, this->focus_y);
            this->focus_glide.Start();
            this->selected = target;
        }
        sound::Play(sound::Effect::Move);
        this->Rebuild();
    }

    void LibraryScreen::OpenWindow(Window window) {
        this->window = window;
        this->window_rise.Start();
        sound::Play(sound::Effect::Open);
        if (window == Window::Game && this->GetSelectedGame() != nullptr) {
            this->game_button = this->GetSelectedGame()->status.is_connected ? GameButton::Launch : GameButton::ToggleHidden;
        }
        this->Rebuild();
    }

    void LibraryScreen::OnInput(u64 keys_down) {
        switch (this->window) {
            case Window::Game: {
                const auto *game = this->GetSelectedGame();
                if (keys_down & HidNpadButton_B) {
                    sound::Play(sound::Effect::Back);
                    this->window = Window::None;
                    this->Rebuild();
                } else if (keys_down & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight)) {
                    const auto button = (keys_down & HidNpadButton_AnyLeft) ? GameButton::Launch : GameButton::ToggleHidden;
                    if (button != this->game_button) {
                        sound::Play(sound::Effect::Move);
                        this->game_button = button;
                        this->Rebuild();
                    }
                } else if ((keys_down & (HidNpadButton_L | HidNpadButton_R)) && this->games.size() > 1) {
                    sound::Play(sound::Effect::Whoosh);
                    this->selected = Wrap(this->selected + ((keys_down & HidNpadButton_L) ? -1 : 1), static_cast<s32>(this->games.size()));
                    this->window_rise.Start();
                    this->Rebuild();
                } else if ((keys_down & HidNpadButton_Y) && game != nullptr) {
                    this->actions.toggle_favorite(*game);
                } else if ((keys_down & HidNpadButton_A) && game != nullptr) {
                    if (this->game_button == GameButton::Launch) {
                        this->LaunchSelected();
                    } else {
                        this->actions.toggle_hidden(*game);
                    }
                }
                return;
            }
            case Window::Message:
                if (this->message_action.run && (keys_down & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight))) {
                    const bool on_action = (keys_down & HidNpadButton_AnyRight) != 0;
                    if (on_action != this->message_on_action) {
                        sound::Play(sound::Effect::Move);
                        this->message_on_action = on_action;
                        this->Rebuild();
                    }
                } else if ((keys_down & HidNpadButton_A) && this->message_action.run && this->message_on_action) {
                    /* The action may show another message; it is taken out first so that one can replace it. */
                    const auto run = std::move(this->message_action.run);
                    this->message_action = {};
                    this->window = this->message_return;
                    this->Rebuild();
                    run();
                } else if (keys_down & (HidNpadButton_A | HidNpadButton_B)) {
                    sound::Play(sound::Effect::Back);
                    this->message_action = {};
                    this->window = this->message_return;
                    this->Rebuild();
                }
                return;
            case Window::Drives:
                if (keys_down & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Y)) {
                    sound::Play(sound::Effect::Back);
                    this->window = Window::None;
                    this->Rebuild();
                }
                return;
            case Window::None:
                break;
        }

        const bool carousel = this->view == View::Carousel;
        if (keys_down & HidNpadButton_Minus) {
            this->actions.exit();
        } else if (keys_down & HidNpadButton_Plus) {
            this->actions.open_settings();
        } else if (keys_down & HidNpadButton_X) {
            this->actions.next_sort();
        } else if (keys_down & HidNpadButton_ZL) {
            this->actions.search();
        } else if ((keys_down & HidNpadButton_B) && !this->browse.query.empty()) {
            this->actions.clear_search();
        } else if ((keys_down & HidNpadButton_B) && this->focus == Focus::Buttons) {
            this->MoveFocus(Focus::Shelf);
        } else if (keys_down & HidNpadButton_Y) {
            this->OpenWindow(Window::Drives);
        } else if (keys_down & HidNpadButton_R) {
            sound::Play(sound::Effect::Whoosh);
            this->view = carousel ? View::Grid : View::Carousel;
            this->focus = Focus::Shelf;
            this->focus_glide = Motion(0.16);
            /* The shelf gathers in from the right; the grid fades in. */
            this->slide_from = carousel ? 0.0 : 2.0;
            this->slide.Start();
            this->enter = Motion(0.18);
            this->enter.Start();
            this->Rebuild();
        } else if (carousel && this->focus == Focus::Buttons) {
            if (keys_down & HidNpadButton_AnyUp) {
                this->MoveFocus(Focus::Shelf);
            } else if (keys_down & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight)) {
                const auto button = (keys_down & HidNpadButton_AnyLeft) ? HomeButton::Launch : HomeButton::Details;
                if (button != this->home_button) {
                    sound::Play(sound::Effect::Move);
                    this->home_button = button;
                    this->Rebuild();
                }
            } else if ((keys_down & HidNpadButton_A) && this->GetSelectedGame() != nullptr) {
                if (this->home_button == HomeButton::Launch) {
                    this->LaunchSelected();
                } else {
                    this->OpenWindow(Window::Game);
                }
            }
        } else if (keys_down & HidNpadButton_AnyLeft) {
            this->Move(-1);
        } else if (keys_down & HidNpadButton_AnyRight) {
            this->Move(1);
        } else if ((keys_down & HidNpadButton_AnyUp) && !carousel) {
            this->Move(-GridColumns);
        } else if (keys_down & HidNpadButton_AnyDown) {
            if (!carousel) {
                this->Move(GridColumns);
            } else if (this->GetSelectedGame() != nullptr) {
                this->home_button = HomeButton::Launch;
                this->MoveFocus(Focus::Buttons);
            }
        } else if ((keys_down & HidNpadButton_A) && this->GetSelectedGame() != nullptr) {
            if (carousel) {
                this->LaunchSelected();
            } else {
                this->OpenWindow(Window::Game);
            }
        }
    }

}
