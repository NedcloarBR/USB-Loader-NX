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
#include <ui/components.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <art.hpp>
#include <manager_settings.hpp>
#include <sound.hpp>
#include <strings.hpp>

namespace ui {

    namespace {

        using strings::Id;

        /* From theme::layout (ui::layout), worked out again on every rebuild (UpdateMetrics). */
        double Edge, HeaderY, HeaderH, FooterY, FooterCenterY;

        /* The carousel: the selected cover large at the left, the next ones queued to its right. */
        struct Pose {
            double x, y, size;
        };
        double ShelfY, BigCover, SmallCover, ShelfGap, ShelfBase, ShelfLineY, QueueX;
        s32 QueueCount;

        /* Grid: covers with their names under them. */
        s32 GridColumns, GridRows;
        double GridCover, GridGapX, GridGapY, GridY, GridX, GridRowH;
        constexpr double NameH = 40;

        /* Under the carousel: where the drive, name, details and buttons go. */
        double InfoY, HomeButtonY;

        /* Covers: GameTDB's front covers, two columns more than the grid, as many rows as fit with the name under them. */
        constexpr double CoverGap = 20;
        s32 CoverCols, CoverRows;
        double CoverW, CoverH, CoverX0;

        s32 CoverColumns() {
            return CoverCols;
        }

        void UpdateMetrics() {
            const Layout &l = layout;
            Edge = l.edge;
            HeaderY = l.toolbar ? l.toolbar_y : 0;
            HeaderH = l.toolbar_h;
            FooterY = DesignHeight - (l.dock ? l.dock_h : 0);
            FooterCenterY = FooterY + l.dock_h / 2;
            ShelfY = l.carousel_y;
            BigCover = l.big_cover;
            SmallCover = l.small_cover;
            ShelfGap = l.carousel_gap;
            ShelfBase = ShelfY + BigCover;
            ShelfLineY = ShelfBase + 7;
            QueueX = Edge + BigCover + ShelfGap;
            QueueCount = l.carousel_count;
            GridColumns = l.grid_columns;
            GridRows = l.grid_rows;
            GridCover = l.grid_cover;
            GridGapX = l.grid_gap_x;
            GridGapY = l.grid_gap_y;
            GridY = l.grid_y;
            GridX = (DesignWidth - GridColumns * GridCover - (GridColumns - 1) * GridGapX) / 2;
            GridRowH = GridCover + 12 + NameH;
            CoverCols = std::clamp(l.grid_columns + 2, 4, 9);
            CoverW = (DesignWidth - 2 * Edge - (CoverCols - 1) * CoverGap) / CoverCols;
            CoverH = CoverW / (1166.0 / 1888.0);
            CoverRows = std::max<s32>(1, static_cast<s32>((FooterY - 56 - GridY + CoverGap) / (CoverH + CoverGap)));
            if (GridY + CoverRows * (CoverH + CoverGap) > FooterY - 56) {
                CoverH = (FooterY - 56 - GridY) / CoverRows - CoverGap;
                CoverW = CoverH * (1166.0 / 1888.0);
            }
            CoverX0 = (DesignWidth - CoverCols * CoverW - (CoverCols - 1) * CoverGap) / 2;
            InfoY = ShelfLineY + 45;
            HomeButtonY = std::min(InfoY + 136, FooterY - 88);
        }

        /* Queued cover k (k >= 1): `scale` times the one before, sinking along the arc towards the last one. */
        double QueuedSize(s32 k) {
            return SmallCover * std::pow(layout.carousel_scale, k - 1);
        }

        double QueuedDrop(double k) {
            const double t = QueueCount > 1 ? std::clamp((k - 1) / (QueueCount - 1), 0.0, 1.5) : 0.0;
            return layout.carousel_arc * t * t;
        }

        /* Slot 0 is the selected cover; slot k > 0 the k-th next one; slots < 0 are off screen to the left. */
        Pose SlotPose(s32 slot) {
            if (slot == 0) {
                return { Edge, ShelfY, BigCover };
            }
            if (slot < 0) {
                /* Off screen to the left, with room to spare: an edge left in view would show as a sliver. */
                const double x = -SmallCover - ShelfGap + (slot + 1) * (SmallCover + ShelfGap);
                return { x, ShelfBase - SmallCover, SmallCover };
            }
            double x = QueueX;
            for (s32 k = 1; k < slot; ++k) {
                x += QueuedSize(k) + ShelfGap;
            }
            const double size = QueuedSize(slot);
            return { x, ShelfBase - size + QueuedDrop(slot), size };
        }

        Pose PoseAt(double position) {
            const s32 low = static_cast<s32>(std::floor(position));
            const double t = position - low;
            const Pose a = SlotPose(low), b = SlotPose(low + 1);
            return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.size + (b.size - a.size) * t };
        }

        /* Windows rise this far (design px) as they open. */
        constexpr double WindowRise = 36;

        /* Placeholder covers draw the name at this size on a 240 px cover and scale it with the cover. */
        constexpr u32 PlaceholderFontSize = 20;
        constexpr double PlaceholderCover = 240;

        /* The dump's format as its extension (XCI, XCZ, NSP, NSZ), or "Installed". */
        std::string FormatName(const usbl::GameStatus &status) {
            if (status.source == usbl::GameSource::Installed) {
                return strings::Get(Id::SourceInstalled);
            }
            const std::string path(status.file_name, strnlen(status.file_name, sizeof(status.file_name)));
            if (const size_t dot = path.rfind('.'); dot != std::string::npos && path.size() - dot == 4) {
                std::string extension = path.substr(dot + 1);
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                if (extension == "XCI" || extension == "XCZ" || extension == "NSP" || extension == "NSZ") {
                    return extension;
                }
            }
            return status.source == usbl::GameSource::Xci ? "XCI" : "NSP";
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
        constexpr double ChipH = 40;
        double ChipY() { return HeaderY + (HeaderH - ChipH) / 2; }

        /* The rating system of the console's region (PEGI in Europe, CERO in Japan, ESRB elsewhere), else any. */
        std::string AgeRating(const gametdb::Entry &entry) {
            if (entry.ratings.empty()) {
                return {};
            }
            SetRegion region = SetRegion_USA;
            if (R_SUCCEEDED(setInitialize())) {
                setGetRegionCode(&region);
                setExit();
            }
            const char *wanted = region == SetRegion_EUR || region == SetRegion_AUS ? "PEGI" : (region == SetRegion_JPN ? "CERO" : "ESRB");
            for (const auto &[system, value] : entry.ratings) {
                if (system == wanted) {
                    return system + " " + value;
                }
            }
            return entry.ratings.front().first + " " + entry.ratings.front().second;
        }

        void SourceIcon(usbl::GameSource source, double cx, double cy, pu::ui::Color c) {
            switch (source) {
                case usbl::GameSource::Xci:       draw::GameCard(cx - 9, cy - 12, 18, 24, c, color::Panel); break;
                case usbl::GameSource::Installed: draw::IconGrid(cx, cy, 22, c);                            break;
                default:                          draw::IconPackage(cx, cy, 24, c);                         break;
            }
        }

    }

    namespace {

        constexpr char ViewKey[] = "view";
        /* Each view's name in manager.ini and in a theme's Views.* (same order as LibraryScreen::View). */
        constexpr const char *ViewNames[] = { "carousel", "grid", "covers", "boxes", "shelves", "list" };
        constexpr Id ViewLabels[] = { Id::ViewCarousel, Id::ViewGrid, Id::ViewCovers, Id::ViewBoxes, Id::ViewShelves, Id::ViewList };

        /* A Switch case is this much wider than tall (GameTDB's covers and cases). */
        constexpr double CaseAspect = 300.0 / 486.0;
        constexpr double CoverAspect = 1166.0 / 1888.0;

        s32 ViewFromName(const std::string &name, s32 fallback) {
            for (size_t i = 0; i < std::size(ViewNames); ++i) {
                if (name == ViewNames[i]) {
                    return static_cast<s32>(i);
                }
            }
            return fallback;
        }

        /* The shelf a game sits on: its folder under usbloadernx/games ("" at the root), "\x01" for an install. */
        std::string FolderOf(const library::Game &game) {
            if (game.status.source == usbl::GameSource::Installed) {
                return "\x01";
            }
            const std::string path(game.status.file_name, strnlen(game.status.file_name, sizeof(game.status.file_name)));
            const size_t slash = path.rfind('/');
            return slash == std::string::npos ? std::string() : path.substr(0, slash);
        }

    }

    LibraryScreen::LibraryScreen(Actions actions) : Layout(), actions(std::move(actions)), app_icon(library::LoadRoundedImage("romfs:/icon.jpg")) {
        this->AddRenderCallback([this]() { this->Animate(); });
        this->view = static_cast<View>(ViewFromName(settings::GetString(ViewKey, theme::DefaultView()), 0));
    }

    std::vector<LibraryScreen::View> LibraryScreen::AvailableViews() const {
        const auto themed = theme::EnabledViews();
        std::vector<View> views;
        for (s32 i = 0; i < static_cast<s32>(View::Count); ++i) {
            const View candidate = static_cast<View>(i);
            if (!themed.empty() && std::find(themed.begin(), themed.end(), ViewNames[i]) == themed.end()) {
                continue;
            }
            views.push_back(candidate);
        }
        /* A theme that names no view this Manager knows still gets the carousel. */
        if (views.empty()) {
            views.push_back(View::Carousel);
        }
        return views;
    }

    s32 LibraryScreen::Columns() const {
        if (this->view == View::Grid) {
            return GridColumns;
        }
        return this->view == View::Covers ? CoverColumns() : 1;
    }

    void LibraryScreen::BuildShelves() {
        this->shelves.clear();
        const bool by_genre = theme::ShelvesByGenre();
        /* Shelf keys: a folder ("" at the root, "\x01" for installs), or a genre's name ("\x02" for none). */
        const auto key_of = [by_genre](const library::Game &game) -> std::string {
            if (!by_genre) {
                return FolderOf(game);
            }
            return game.in_gametdb && !game.gametdb.genres.empty() ? strings::Genre(game.gametdb.genres.front()) : std::string("\x02");
        };
        std::vector<std::string> keys;
        for (s32 i = 0; i < static_cast<s32>(this->games.size()); ++i) {
            const std::string key = key_of(this->games[i]);
            auto found = std::find(keys.begin(), keys.end(), key);
            if (found == keys.end()) {
                keys.push_back(key);
                this->shelves.push_back({ key, {} });
                found = keys.end() - 1;
            }
            this->shelves[found - keys.begin()].games.push_back(i);
        }
        /* The root first, the folders or genres by name, installs and games without a genre last. */
        std::sort(this->shelves.begin(), this->shelves.end(), [](const Shelf &a, const Shelf &b) {
            const auto rank = [](const std::string &name) { return name.empty() ? 0 : (name[0] == '\x01' || name[0] == '\x02' ? 2 : 1); };
            return rank(a.name) != rank(b.name) ? rank(a.name) < rank(b.name) : strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
        });
        for (auto &shelf : this->shelves) {
            if (shelf.name.empty()) {
                shelf.name = strings::Get(Id::ShelfRoot);
            } else if (shelf.name == "\x01") {
                shelf.name = strings::Get(Id::ShelfInstalled);
            } else if (shelf.name == "\x02") {
                shelf.name = strings::Get(Id::ShelfNoGenre);
            }
        }
    }

    std::pair<s32, s32> LibraryScreen::ShelfPosition() const {
        for (s32 shelf = 0; shelf < static_cast<s32>(this->shelves.size()); ++shelf) {
            const auto &games = this->shelves[shelf].games;
            if (const auto found = std::find(games.begin(), games.end(), this->selected); found != games.end()) {
                return { shelf, static_cast<s32>(found - games.begin()) };
            }
        }
        return { 0, 0 };
    }

    void LibraryScreen::SwitchView(s32 delta) {
        const auto views = this->AvailableViews();
        const auto current = std::find(views.begin(), views.end(), this->view);
        const s32 at = current != views.end() ? static_cast<s32>(current - views.begin()) : 0;
        const View next = views[Wrap(at + delta, static_cast<s32>(views.size()))];
        if (next == this->view) {
            return;
        }
        sound::Play(sound::Effect::Whoosh);
        /* The pictures fly from where they were drawn last to their places in the new view. */
        this->fly_from = std::move(this->last_rects);
        this->last_rects.clear();
        this->fly.Start();
        this->view = next;
        settings::SetString(ViewKey, ViewNames[static_cast<size_t>(next)]);
        this->focus = Focus::Shelf;
        this->focus_glide = Motion(0.16);
        this->first_spine = 0;
        this->slide_from = 0.0;
        this->slide = Motion(0.24);
        this->Rebuild();
    }

    void LibraryScreen::SetAllGames(std::vector<library::Game> games) {
        this->all_games = std::move(games);
    }

    void LibraryScreen::Show(std::vector<library::Game> games, std::vector<usbl::DriveStatus> drives, std::string message, Browse browse) {
        /* A theme edited while open may have new pictures under the same names. */
        this->theme_images.clear();
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

        this->spine_names.assign(this->games.size(), {});
        this->BuildShelves();
        if (const auto views = this->AvailableViews(); std::find(views.begin(), views.end(), this->view) == views.end()) {
            this->view = views.front();
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
        this->hits.clear();
        this->taps.clear();
        this->last_rects.clear();
        UpdateMetrics();
        AddPaint(*this, []() { draw::Background(); });
        this->UpdateBackdrop();
        if (this->backdrop != nullptr) {
            AddPaint(*this, [this]() {
                const double t = this->backdrop_fade.Progress();
                if (this->backdrop_previous != nullptr && t < 1.0) {
                    draw::Image(this->backdrop_previous->Get(), 0, 0, DesignWidth, DesignHeight, static_cast<u8>((1.0 - t) * 255));
                }
                draw::Image(this->backdrop->Get(), 0, 0, DesignWidth, DesignHeight, static_cast<u8>(t * 255));
                /* Dimmed so text stays readable over any picture. */
                draw::Rect(0, 0, DesignWidth, DesignHeight, color::WithAlpha(color::Ground, 0xB8));
            });
        }

        if (layout.toolbar) {
            this->AddHeader();
        }
        if (this->games.empty()) {
            const std::string text = this->message.empty() ? strings::Get(Id::NoGames) : this->message;
            AddText(*this, DesignWidth / 2, 330, text, 21, color::MutedSoft, Align::Center, 1100);
        } else if (this->view == View::Boxes) {
            this->AddCoverFlow();
        } else if (this->view == View::Carousel) {
            this->AddCarousel();
        } else if (this->view == View::Covers) {
            this->AddCovers();
        } else if (this->view == View::Shelves) {
            this->AddShelves();
        } else if (this->view == View::List) {
            this->AddList();
        } else {
            this->AddGrid();
        }
        if (this->window == Window::None && layout.dock) {
            this->AddFooter();
        }
        if (this->window == Window::None) {
            this->AddFreeComponents();
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

    void LibraryScreen::UpdateBackdrop() {
        const auto *game = this->GetSelectedGame();
        SDL_Texture *source = nullptr;
        if (game != nullptr && theme::ShowsGameBackdrop()) {
            source = art::GetTexture(game->status.application_id, art::Kind::Cover);
            if (source == nullptr && game->icon != nullptr) {
                source = game->icon->Get();
            }
        }
        if (source == this->backdrop_source) {
            return;
        }
        this->backdrop_source = source;
        this->backdrop_previous = this->backdrop;
        SDL_Texture *blurred = source != nullptr ? draw::MakeBlurred(source) : nullptr;
        this->backdrop = blurred != nullptr ? pu::sdl2::TextureHandle::New(blurred) : pu::sdl2::TextureHandle::Ref();
        this->backdrop_fade.Start();
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

    void LibraryScreen::Fly(s32 index, double *x, double *y, double *w, double *h) {
        if (const double t = this->fly.Progress(); t < 1.0) {
            if (const auto from = this->fly_from.find(index); from != this->fly_from.end()) {
                const Rect &r = from->second;
                *x = r.x + (*x - r.x) * t;
                *y = r.y + (*y - r.y) * t;
                *w = r.w + (*w - r.w) * t;
                *h = r.h + (*h - r.h) * t;
            }
        }
        this->last_rects[index] = { *x, *y, *w, *h };
    }

    void LibraryScreen::PaintCover(size_t index, double x, double y, double size, double opacity) {
        if (this->window == Window::None) {
            double w = size, h = size;
            this->Fly(static_cast<s32>(index), &x, &y, &w, &h);
            size = w;
        }
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

    void LibraryScreen::AddCovers() {
        const s32 count = static_cast<s32>(this->games.size());
        const s32 row = this->selected / CoverCols;
        if (row < this->first_row) {
            this->first_row = row;
        } else if (row >= this->first_row + CoverRows) {
            this->first_row = row - CoverRows + 1;
        }
        const auto cell_x = [](s32 cell) { return CoverX0 + (cell % CoverCols) * (CoverW + CoverGap); };
        const auto cell_y = [](s32 cell) { return GridY + static_cast<double>(cell / CoverCols) * (CoverH + CoverGap); };

        for (s32 cell = 0; cell < CoverCols * CoverRows; ++cell) {
            const s32 index = this->first_row * CoverCols + cell;
            if (index >= count) {
                break;
            }
            const double x = cell_x(cell), y = cell_y(cell);
            this->hits.push_back({ x, y, CoverW, CoverH, index });
            const bool is_selected = index == this->selected;
            AddPaint(*this, [this, index, x, y, is_selected]() {
                const double lift = is_selected ? Float(2) * this->focus_glide.Progress() : 0.0;
                const auto &game = this->games[index];
                if (SDL_Texture *cover = art::GetTexture(game.status.application_id, art::Kind::Cover); cover != nullptr) {
                    double fx = x, fy = y + lift, fw = CoverW, fh = CoverH;
                    this->Fly(index, &fx, &fy, &fw, &fh);
                    draw::Image(cover, fx, fy, fw, fh);
                    if (!game.status.is_connected) {
                        draw::Rect(x, y + lift, CoverW, CoverH, color::WithAlpha(color::Ground, 0x80));
                    }
                } else {
                    /* No cover: the game's icon on a card of the cover's shape. */
                    draw::RoundRect(x, y + lift, CoverW, CoverH, 10, color::Panel);
                    this->PaintCover(index, x + 6, y + lift + (CoverH - CoverW) / 2 + 6, CoverW - 12, 1.0);
                }
            });
        }

        const s32 cell = this->selected - this->first_row * CoverCols;
        this->focus_x = cell_x(cell);
        this->focus_y = cell_y(cell);
        AddPaint(*this, [this]() {
            const double x = this->focus_glide.Between(this->focus_from_x, this->focus_x);
            const double y = this->focus_glide.Between(this->focus_from_y, this->focus_y) + Float(2) * this->focus_glide.Progress();
            draw::FocusRing(x, y, CoverW, CoverH, 8);
        });

        /* The selected game's name under the covers. */
        const double name_y = GridY + CoverRows * (CoverH + CoverGap) - CoverGap + 14;
        if (theme::BackgroundImage() != nullptr) {
            AddPaint(*this, [name_y]() { draw::RoundRect(Edge - 12, name_y - 6, DesignWidth - 2 * Edge + 24, 40, 12, color::WithAlpha(color::Ground, 0xC0)); });
        }
        const auto &game = this->games[this->selected];
        AddText(*this, DesignWidth / 2, name_y, game.name, 21, color::Text, Align::Center, DesignWidth - 2 * Edge);
    }

    void LibraryScreen::AddCoverFlow() {
        const s32 count = static_cast<s32>(this->games.size());
        constexpr double CenterX = DesignWidth / 2.0, Height = 300, SideGap = 64, Step = 78, Turn = 0.62;
        const double base = ShelfY + Height + 10;

        AddPaint(*this, [this, count, base]() {
            const double shift = this->slide.Between(this->slide_from, 0.0);
            const s32 reach = std::min(count - 1, 5);
            /* The case's picture: GameTDB's case, else its cover, else the game's icon. */
            const auto picture = [this](s32 index) -> SDL_Texture * {
                const auto &game = this->games[index];
                if (SDL_Texture *box = art::GetTexture(game.status.application_id, art::Kind::Box); box != nullptr) {
                    return box;
                }
                if (SDL_Texture *cover = art::GetTexture(game.status.application_id, art::Kind::Cover); cover != nullptr) {
                    return cover;
                }
                return game.icon != nullptr ? game.icon->Get() : nullptr;
            };
            /* Farthest first, so nearer cases cover them; the selected one last. */
            std::vector<s32> order;
            for (s32 offset = -reach; offset <= reach; ++offset) {
                if (count > 1 || offset == 0) {
                    order.push_back(offset);
                }
            }
            std::stable_sort(order.begin(), order.end(), [shift](s32 a, s32 b) { return std::abs(a + shift) > std::abs(b + shift); });
            for (const s32 offset : order) {
                const double position = offset + shift;
                const double distance = std::abs(position);
                if (distance > 5.5) {
                    continue;
                }
                const s32 index = Wrap(this->selected + offset, count);
                SDL_Texture *texture = picture(index);
                if (texture == nullptr) {
                    continue;
                }
                s32 tw = 0, th = 0;
                SDL_QueryTexture(texture, nullptr, nullptr, &tw, &th);
                const double aspect = th > 0 ? static_cast<double>(tw) / th : 1.0;
                const double h = Height * (distance < 1 ? 1.0 - 0.12 * distance : 0.88), w = h * aspect;
                /* How far it is turned away (0 facing, 1 at the side), and where its middle is. */
                const double turn = std::min(distance, 1.0);
                const double side_x = Height * CaseAspect / 2 + SideGap + (distance - 1) * Step;
                const double cx = CenterX + (position < 0 ? -1 : 1) * (distance < 1 ? side_x * distance : side_x);
                const double pw = w * (1.0 - Turn * turn), far_h = h * (1.0 - 0.2 * turn);
                const double left = cx - pw / 2, right = cx + pw / 2, top = base - h, inset = (h - far_h) / 2;
                /* The edge nearer the middle keeps its height; the far edge shrinks (perspective). */
                const bool far_right = position > 0;
                const double top_left = far_right ? top : top + inset, bottom_left = far_right ? base : base - inset;
                const double top_right = far_right ? top + inset : top, bottom_right = far_right ? base - inset : base;
                const u8 alpha = static_cast<u8>(std::clamp(1.0 - (distance - 4.0), 0.0, 1.0) * (this->games[index].status.is_connected ? 255 : 150));
                /* Switching views: the whole case (and its reflection) flies in from where the picture was. */
                double fx = left, fy = top, fw = pw, fh = h;
                this->Fly(index, &fx, &fy, &fw, &fh);
                const auto fly_x = [&](double x) { return fx + (x - left) / pw * fw; };
                const auto fly_y = [&](double y) { return fy + (y - top) / h * fh; };
                /* The reflection: the case upside down under itself, fading out. */
                const double reflect = h * 0.3;
                draw::ImageQuad(texture, { { fly_x(left), fly_y(bottom_left), static_cast<u8>(alpha * 0.32) }, { fly_x(right), fly_y(bottom_right), static_cast<u8>(alpha * 0.32) },
                                           { fly_x(right), fly_y(bottom_right + reflect), 0 }, { fly_x(left), fly_y(bottom_left + reflect), 0 } }, true, 0.3);
                draw::ImageQuad(texture, { { fly_x(left), fly_y(top_left), alpha }, { fly_x(right), fly_y(top_right), alpha },
                                           { fly_x(right), fly_y(bottom_right), alpha }, { fly_x(left), fly_y(bottom_left), alpha } });
            }
            if (count > 0 && std::abs(shift) < 0.05) {
                if (SDL_Texture *texture = picture(this->selected); texture != nullptr) {
                    s32 tw = 0, th = 0;
                    SDL_QueryTexture(texture, nullptr, nullptr, &tw, &th);
                    const double w = Height * (th > 0 ? static_cast<double>(tw) / th : 1.0);
                    draw::FocusRing(CenterX - w / 2, base - Height, w, Height, 8);
                }
            }
        });

        /* Taps: the middle case, and a band on each side for the next and previous ones. */
        if (count > 0) {
            const double w = Height * CaseAspect;
            this->hits.push_back({ CenterX - w / 2, base - Height, w, Height, this->selected });
            if (count > 1) {
                this->hits.push_back({ CenterX + w / 2, base - Height, DesignWidth - CenterX - w / 2, Height, Wrap(this->selected + 1, count) });
                this->hits.push_back({ 0, base - Height, CenterX - w / 2, Height, Wrap(this->selected - 1, count) });
            }
        }

        /* Under the cases: the selected game's name and what it is, centered. */
        const auto &game = this->games[this->selected];
        const double info_y = base + Height * 0.3 + 8;
        if (theme::BackgroundImage() != nullptr) {
            AddPaint(*this, [info_y]() { draw::RoundRect(Edge, info_y - 8, DesignWidth - 2 * Edge, 96, 20, color::WithAlpha(color::Ground, 0xC0)); });
        }
        AddText(*this, DesignWidth / 2, info_y, game.name, 30, color::Text, Align::Center, DesignWidth - 2 * Edge);
        std::string details = FormatName(game.status);
        if (!game.display_version.empty()) {
            details += "  ·  " + strings::Format(Id::Version, { { "VERSION", game.display_version } });
        }
        details += "  ·  " + strings::Size(game.status.size);
        if (const auto [alert, is_alert] = GameAlert(game); !alert.empty()) {
            details += "  ·  " + alert;
        }
        AddText(*this, DesignWidth / 2, info_y + 46, details, 16, color::Muted, Align::Center, DesignWidth - 2 * Edge);
    }

    void LibraryScreen::AddList() {
        constexpr double RowH = 50, IconSize = 38, HeadH = 30;
        const double top = GridY, list_w = DesignWidth - 2 * Edge;
        const s32 rows = std::max<s32>(1, static_cast<s32>((FooterY - 12 - top - HeadH) / RowH));
        const s32 count = static_cast<s32>(this->games.size());
        if (this->selected < this->first_row) {
            this->first_row = this->selected;
        } else if (this->selected >= this->first_row + rows) {
            this->first_row = this->selected - rows + 1;
        }

        /* Columns from the right edge: played, size, version, drive; the name takes the rest. */
        const double played_x = Edge + list_w - 170, size_x = played_x - 110, version_x = size_x - 110, drive_x = version_x - 220;
        const double name_x = Edge + 16 + IconSize + 14, name_w = drive_x - name_x - 16;
        const struct {
            double x;
            Id label;
        } columns[] = { { name_x, Id::ListColumnGame }, { drive_x, Id::ListColumnDrive }, { version_x, Id::RowVersion }, { size_x, Id::RowSize }, { played_x, Id::RowPlayTime } };
        if (theme::BackgroundImage() != nullptr || this->backdrop != nullptr) {
            AddPaint(*this, [top, list_w, rows]() { draw::RoundRect(Edge - 8, top - 6, list_w + 16, HeadH + rows * RowH + 12, 16, color::WithAlpha(color::Ground, 0xC0)); });
        }
        for (const auto &column : columns) {
            AddText(*this, column.x, top, strings::Get(column.label), 14, color::Muted);
        }
        AddPaint(*this, [top, list_w]() { draw::Rect(Edge, top + HeadH - 6, list_w, 1, color::Track); });

        for (s32 slot = 0; slot < rows && this->first_row + slot < count; ++slot) {
            const s32 index = this->first_row + slot;
            const auto &game = this->games[index];
            const double y = top + HeadH + slot * RowH;
            this->hits.push_back({ Edge, y, list_w, RowH, index });
            const bool is_selected = index == this->selected;
            AddPaint(*this, [this, index, y, list_w, is_selected]() {
                if (is_selected && !draw::Skinned(theme::Skin::CardFocused, Edge, y, list_w, RowH - 4)) {
                    draw::RoundRect(Edge, y, list_w, RowH - 4, 12, color::Raised);
                }
                this->PaintCover(index, Edge + 16, y + (RowH - 4 - IconSize) / 2, IconSize, 1.0);
            });
            const pu::ui::Color text = game.status.is_connected ? color::Text : color::Muted;
            const double text_y = y + (RowH - 4 - 22) / 2;
            AddText(*this, name_x, text_y, game.name, 17, text, Align::Left, name_w);
            AddText(*this, drive_x, text_y + 2, game.status.is_connected ? DriveName(this->drives, game.status) : strings::Get(Id::Disconnected), 15, color::Muted, Align::Left, 200);
            AddText(*this, version_x, text_y + 2, game.display_version.empty() ? "—" : game.display_version, 15, color::Muted, Align::Left, 100);
            AddText(*this, size_x, text_y + 2, strings::Size(game.status.size), 15, color::Muted, Align::Left, 100);
            AddText(*this, played_x, text_y + 2, PlayTime(game.play), 15, color::Muted, Align::Left, 170);
        }
        const double ring_y = top + HeadH + (this->selected - this->first_row) * RowH;
        AddPaint(*this, [ring_y, list_w]() { draw::FocusRing(Edge, ring_y, list_w, RowH - 4, 12, 3); });
        if (this->first_row > 0) {
            AddPaint(*this, [top, list_w]() { draw::ChevronVertical(Edge + list_w / 2, top + HeadH - 14, 16, false, color::Primary); });
        }
        if (this->first_row + rows < count) {
            AddPaint(*this, [top, list_w, rows]() { draw::ChevronVertical(Edge + list_w / 2, top + HeadH + rows * RowH + 2, 16, true, color::Primary); });
        }
    }

    void LibraryScreen::AddShelves() {
        if (this->shelves.empty()) {
            return;
        }
        const auto [shelf_index, place] = this->ShelfPosition();
        const Shelf &shelf = this->shelves[shelf_index];
        const double top = GridY, area_top = top + 52, area_bottom = FooterY - 76, height = area_bottom - area_top;

        /* The shelf's name, how many games it holds and which of the shelves it is (Up/Down). */
        const auto title = AddText(*this, Edge, top, shelf.name, 26, color::Text, Align::Left, 700);
        const std::string games = shelf.games.size() == 1 ? strings::Get(Id::ShelfOneGame) : strings::Format(Id::ShelfGames, { { "COUNT", std::to_string(shelf.games.size()) } });
        AddText(*this, Edge + title->GetWidth() / Scale + 18, top + 10, games, 15, color::Muted);
        if (this->shelves.size() > 1) {
            AddText(*this, DesignWidth - Edge, top + 8, std::to_string(shelf_index + 1) + " / " + std::to_string(this->shelves.size()), 17, color::Muted, Align::Right);
        }

        /* Left: the selected game's front cover. */
        const double cover_w = height * CoverAspect;
        const s32 selected = this->selected;
        AddPaint(*this, [this, selected, area_top, height, cover_w]() {
            const auto &game = this->games[selected];
            if (SDL_Texture *cover = art::GetTexture(game.status.application_id, art::Kind::Cover); cover != nullptr) {
                double x = Edge, y = area_top, w = cover_w, h = height;
                this->Fly(selected, &x, &y, &w, &h);
                draw::Image(cover, x, y, w, h);
            } else {
                draw::RoundRect(Edge, area_top, cover_w, height, 12, color::Panel);
                this->PaintCover(selected, Edge + 8, area_top + (height - cover_w) / 2 + 8, cover_w - 16, 1.0);
            }
        });

        /* Right: the spines side by side on a plank; the selected one pulled out. */
        const double spines_x = Edge + cover_w + 40, spines_w = DesignWidth - Edge - spines_x;
        constexpr double Lift = 22;
        const double spine_h = height - Lift, spine_w = std::max(24.0, spine_h * 36.0 / 570.0), step = spine_w + 4;
        const s32 visible = std::max<s32>(1, static_cast<s32>((spines_w + 4) / step));
        const s32 count = static_cast<s32>(shelf.games.size());
        this->first_spine = std::clamp(this->first_spine, std::max(0, place - visible + 1), place);
        this->first_spine = std::clamp(this->first_spine, 0, std::max(0, count - visible));

        for (s32 slot = 0; slot < visible && this->first_spine + slot < count; ++slot) {
            const s32 index = shelf.games[this->first_spine + slot];
            if (this->spine_names[index] == nullptr) {
                const std::string name = Fit(this->games[index].name, 13, spine_h - 20);
                this->spine_names[index] = pu::sdl2::TextureHandle::New(pu::ui::render::RenderText(Font(13), name, color::TextSoft));
            }
            const double spine_x = spines_x + slot * step;
            this->hits.push_back({ spine_x, area_top, step, height, index });
            const bool is_selected = index == this->selected;
            AddPaint(*this, [this, index, spine_x, area_top, spine_w, spine_h, is_selected]() {
                const double rest_y = area_top + Lift - (is_selected ? Lift * this->focus_glide.Progress() : 0.0);
                const auto &game = this->games[index];
                /* The selected game flies to its cover; the others to their spines. */
                double x = spine_x, y = rest_y, w = spine_w, h = spine_h;
                if (!is_selected) {
                    this->Fly(index, &x, &y, &w, &h);
                }
                if (SDL_Texture *spine = art::GetSpineTexture(game.status.application_id); spine != nullptr) {
                    draw::Image(spine, x, y, w, h);
                } else {
                    draw::RoundRect(x, y, w, h, 4, is_selected ? color::Raised : color::Panel);
                    draw::RoundOutline(x, y, w, h, 4, 1, color::Track);
                    if (const auto &name = this->spine_names[index]; name != nullptr) {
                        s32 tw = 0, th = 0;
                        SDL_QueryTexture(name->Get(), nullptr, nullptr, &tw, &th);
                        draw::ImageTurned(name->Get(), x + w / 2, y + h / 2, tw / Scale, th / Scale, 90);
                    }
                }
                if (!game.status.is_connected) {
                    draw::Rect(x, y, w, h, color::WithAlpha(color::Ground, 0x80));
                }
                if (is_selected) {
                    draw::FocusRing(x, y, w, h, 4, 4);
                }
            });
        }
        AddPaint(*this, [spines_x, spines_w, area_top, height]() { draw::RoundRect(spines_x - 12, area_top + height, spines_w + 24, 8, 4, color::Track); });
        if (this->first_spine > 0) {
            AddPaint(*this, [spines_x, area_top, height]() { draw::ChevronLeft(spines_x - 26, area_top + height / 2, 18, color::Muted); });
        }
        if (this->first_spine + visible < count) {
            AddPaint(*this, [area_top, height]() { draw::Chevron(DesignWidth - Edge + 18, area_top + height / 2, 18, color::Muted); });
        }

        /* Under the shelf: the selected game's name and what it is. */
        const auto &game = this->games[this->selected];
        AddText(*this, Edge, area_bottom + 18, game.name, 21, color::Text, Align::Left, DesignWidth - 2 * Edge);
        std::string details = FormatName(game.status);
        if (!game.display_version.empty()) {
            details += "  ·  " + strings::Format(Id::Version, { { "VERSION", game.display_version } });
        }
        if (const auto [alert, is_alert] = GameAlert(game); !alert.empty()) {
            details += "  ·  " + alert;
        }
        AddText(*this, Edge, area_bottom + 48, details, 15, color::Muted, Align::Left, DesignWidth - 2 * Edge);
    }

    void LibraryScreen::AddHeader() {
        namespace cmp = components;
        const auto logo = cmp::Place("Home", "logo", { true, Edge, HeaderY + (HeaderH - 40) / 2, 40, 40 });
        if (logo.visible) {
            AddPaint(*this, [this, logo]() {
                if (this->app_icon != nullptr) {
                    draw::Image(this->app_icon->Get(), logo.x, logo.y, logo.w, logo.h);
                }
            });
        }
        const auto title = cmp::Place("Home", "title", { true, Edge + 52, HeaderY + HeaderH / 2 - 13, 400, 28, 21 });
        if (title.visible) {
            AddText(*this, cmp::TextX(title), title.y, strings::Get(Id::AppTitle), title.font_size, color::Text, title.align, title.w);
        }

        /* Right to left: the drives, the search (ZL, or the query with B to clear it), the order (X), which games show
         * (ZR). Each chip can be moved on its own; its default place follows the ones to its right. */
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
        const auto background = [this](const cmp::Placement &place, bool outline, bool primary) {
            AddPaint(*this, [place, outline, primary]() {
                if (!draw::Skinned(theme::Skin::Chip, place.x, place.y, place.w, place.h)) {
                    draw::RoundRect(place.x, place.y, place.w, place.h, place.h / 2, color::Panel);
                    if (outline) {
                        draw::RoundOutline(place.x, place.y, place.w, place.h, place.h / 2, primary ? 1.5 : 1, primary ? color::Primary : color::Track);
                    }
                }
            });
        };
        const auto text_y = [](const cmp::Placement &place) { return place.y + (place.h - 20) / 2; };

        double next_right = DesignWidth - Edge;
        const double drives_w = TextWidth(drives, 15) + 50;
        const auto drives_chip = cmp::Place("Home", "chip.drives", { layout.free_space, next_right - drives_w, ChipY(), drives_w, ChipH, 15 });
        if (drives_chip.visible) {
            background(drives_chip, true, false);
            AddPaint(*this, [drives_chip, usage, has_drives]() { draw::Circle(drives_chip.x + 20, drives_chip.y + drives_chip.h / 2, 4, has_drives ? draw::UsageColor(usage) : color::Faint); });
            AddText(*this, drives_chip.x + 34, text_y(drives_chip), drives, drives_chip.font_size, color::TextSoft, Align::Left, drives_chip.w - 44);
            next_right = layout.free_space ? next_right - drives_w - 10 : next_right;
        }

        constexpr double MaxChipText = 300;
        const bool searching = !this->browse.query.empty();
        const std::string search = Fit(searching ? "“" + this->browse.query + "”" : strings::Get(Id::Search), 15, MaxChipText);
        const double search_glyph = searching ? 24 : TextWidth("ZL", 13) + 14;
        const double search_w = TextWidth(search, 15) + 60 + (searching ? 0 : 10);
        const auto search_chip = cmp::Place("Home", "chip.search", { true, next_right - search_w, ChipY(), search_w, ChipH, 15 });
        if (search_chip.visible) {
            background(search_chip, searching, true);
            AddButtonHint(*this, search_chip.x + 8 + search_glyph / 2, search_chip.y + search_chip.h / 2, searching ? "B" : "ZL");
            AddText(*this, search_chip.x + 18 + search_glyph, text_y(search_chip), search, search_chip.font_size, searching ? color::Text : color::TextSoft, Align::Left, search_chip.w - 28 - search_glyph);
            next_right -= search_w + 10;
        }

        const std::string sort = Fit(this->browse.sort_name, 15, MaxChipText);
        const double sort_w = TextWidth(sort, 15) + 60;
        const auto sort_chip = cmp::Place("Home", "chip.sort", { true, next_right - sort_w, ChipY(), sort_w, ChipH, 15 });
        if (sort_chip.visible) {
            background(sort_chip, false, false);
            AddButtonHint(*this, sort_chip.x + 21, sort_chip.y + sort_chip.h / 2, "X");
            AddText(*this, sort_chip.x + 44, text_y(sort_chip), sort, sort_chip.font_size, color::TextSoft, Align::Left, sort_chip.w - 54);
            next_right -= sort_w + 10;
        }

        const std::string filter = Fit(this->browse.filter_name, 15, MaxChipText);
        const double filter_glyph = TextWidth("ZR", 13) + 14;
        const double filter_w = TextWidth(filter, 15) + filter_glyph + 36;
        const auto filter_chip = cmp::Place("Home", "chip.filter", { true, next_right - filter_w, ChipY(), filter_w, ChipH, 15 });
        if (filter_chip.visible) {
            background(filter_chip, false, false);
            AddButtonHint(*this, filter_chip.x + 8 + filter_glyph / 2, filter_chip.y + filter_chip.h / 2, "ZR");
            AddText(*this, filter_chip.x + 18 + filter_glyph, text_y(filter_chip), filter, filter_chip.font_size, color::TextSoft, Align::Left, filter_chip.w - 28 - filter_glyph);
        }
    }

    void LibraryScreen::AddCarousel() {
        const s32 count = static_cast<s32>(this->games.size());
        /* Each game shows once: the selected one, then up to five after it, and the one before it off screen. */
        const s32 ahead = std::min(count - 1, QueueCount);
        const s32 behind = count > 1 ? 1 : 0;

        if (layout.carousel_arc == 0) {
            AddPaint(*this, []() { draw::Rect(0, ShelfLineY, DesignWidth, 2, color::Track); });
        }
        AddPaint(*this, [this, count, ahead, behind]() {
            const double shift = this->slide.Between(this->slide_from, 0.0);
            const double bob = Float(4) * this->slide.Progress();
            for (s32 offset = -behind - 1; offset <= ahead + 1; ++offset) {
                const double position = offset + shift;
                if (offset < -behind || offset > ahead || position < -1.5 || position > QueueCount + 1.0) {
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
                const s32 index = Wrap(this->selected + offset, count);
                this->PaintCover(index, pose.x, pose.y + lift, pose.size, fade);
                /* Its reflection on the shelf, upside down and fading out. */
                if (const auto &icon = this->games[index].icon; icon != nullptr) {
                    const double bottom = pose.y + pose.size + 9, reflect = std::min(pose.size * 0.16, 34.0);
                    const u8 alpha = static_cast<u8>(0x48 * fade);
                    draw::ImageQuad(icon->Get(), { { pose.x, bottom, alpha }, { pose.x + pose.size, bottom, alpha },
                                                   { pose.x + pose.size, bottom + reflect, 0 }, { pose.x, bottom + reflect, 0 } }, true, reflect / pose.size);
                }
            }
            if (this->focus == Focus::Shelf) {
                draw::FocusRing(Edge, ShelfY + bob, BigCover, BigCover, BigCover * IconCornerRatio);
            }
        });


        for (s32 slot = 0; slot <= ahead; ++slot) {
            const Pose pose = SlotPose(slot);
            this->hits.push_back({ pose.x, pose.y, pose.size, pose.size, Wrap(this->selected + slot, count) });
        }

        const auto &game = this->games[this->selected];
        const auto &status = game.status;
        constexpr double InfoW = 820;
        namespace cmp = components;
        /* Each part under the carousel can be moved; over a picture each gets its own veil, and all of them fade in
         * as the covers settle. */
        std::vector<cmp::Placement> placed;
        const auto place = [&placed](const char *id, cmp::Placement defaults) {
            const auto result = cmp::Place("Home", id, defaults);
            if (result.visible) {
                placed.push_back(result);
            }
            return result;
        };
        const auto drive = place("info.drive", { true, Edge, InfoY, InfoW, 20, 15 });
        const auto name = place("info.name", { true, Edge - 2, InfoY + 22, InfoW, 58, 46 });
        const auto details = place("info.chips", { true, Edge, InfoY + 84, InfoW, 30, 14 });
        const auto stats = place("info.stats", { true, Edge, InfoY + 132, InfoW, 22, 15 });
        const auto [alert, is_alert] = GameAlert(game);
        const auto warning = alert.empty() ? cmp::Placement{ false } : place("info.alert", { true, Edge, InfoY + 160, InfoW, 22, 15 });
        if (theme::BackgroundImage() != nullptr) {
            AddPaint(*this, [placed]() {
                for (const auto &part : placed) {
                    draw::RoundRect(part.x - 12, part.y - 6, part.w + 24, part.h + 12, 12, color::WithAlpha(color::Ground, 0xC0));
                }
            });
        }

        if (drive.visible) {
            AddText(*this, cmp::TextX(drive), drive.y, status.is_connected ? strings::Format(Id::OnDrive, { { "DRIVE", DriveName(this->drives, status) } }) : strings::Get(Id::Disconnected), drive.font_size, color::Muted, drive.align, drive.w);
        }
        if (name.visible) {
            AddText(*this, cmp::TextX(name), name.y, game.name, name.font_size, color::Text, name.align, name.w);
        }

        if (details.visible) {
            std::vector<std::string> chips = { FormatName(status) };
            if (!game.display_version.empty()) {
                chips.push_back(strings::Format(Id::Version, { { "VERSION", game.display_version } }));
            }
            if (status.add_on_count > 0) {
                chips.push_back(strings::Count(status.add_on_count, Id::AddOnOne, Id::AddOnMany));
            }
            chips.push_back(strings::Size(status.size));
            std::vector<std::pair<double, double>> chip_boxes;
            double total = -8;
            for (const auto &chip : chips) {
                const double w = TextWidth(chip, details.font_size) + 24;
                chip_boxes.push_back({ 0, w });
                total += w + 8;
            }
            /* The row lines up as the placement's alignment says. */
            double chip_x = details.align == Align::Center ? details.x + (details.w - total) / 2 : (details.align == Align::Right ? details.x + details.w - total : details.x);
            for (auto &box : chip_boxes) {
                box.first = chip_x;
                chip_x += box.second + 8;
            }
            AddPaint(*this, [chip_boxes, details]() {
                for (const auto &[x, w] : chip_boxes) {
                    if (!draw::Skinned(theme::Skin::Chip, x, details.y, w, details.h)) {
                        draw::RoundRect(x, details.y, w, details.h, 8, color::Panel);
                    }
                }
            });
            for (size_t i = 0; i < chips.size(); ++i) {
                AddText(*this, chip_boxes[i].first + 12, details.y + (details.h - details.font_size * 1.3) / 2, chips[i], details.font_size, color::TextSoft);
            }
        }

        if (stats.visible) {
            AddPaint(*this, [stats]() { draw::IconClock(stats.x + 9, stats.y + 10, 18, color::Muted); });
            std::string line = PlayTime(game.play);
            if (game.play.last_played != 0) {
                line += "      " + strings::Format(Id::LastPlayed, { { "DATE", FormatDate(game.play.last_played) } });
            }
            AddText(*this, stats.x + 26, stats.y, line, stats.font_size, color::Muted, Align::Left, stats.w - 26);
        }
        if (warning.visible) {
            const pu::ui::Color tint = is_alert ? color::Alert : color::Muted;
            AddPaint(*this, [tint, warning]() { draw::IconWarning(warning.x + 9, warning.y + 10, 18, tint); });
            AddText(*this, warning.x + 26, warning.y, alert, warning.font_size, tint, Align::Left, warning.w - 26);
        }

        /* Down moves the focus from the shelf to Launch and Details; A on the shelf launches. The button A would press
         * is the filled one with the A. */
        const std::string launch = strings::Get(Id::Launch), details_label = strings::Get(Id::Details);
        constexpr double ButtonH = 64;
        const double launch_w = TextWidth(launch, 20) + 96, details_w = TextWidth(details_label, 20) + 96;
        const auto details_button = cmp::Place("Home", "button.details", { true, DesignWidth - Edge - details_w, HomeButtonY, details_w, ButtonH });
        const auto launch_button = cmp::Place("Home", "button.launch", { true, details_button.x - 14 - launch_w, HomeButtonY, launch_w, ButtonH });
        const bool on_buttons = this->focus == Focus::Buttons;
        const bool details_active = on_buttons && this->home_button == HomeButton::Details;
        if (launch_button.visible) {
            this->AddActionButton(launch_button.x, launch_button.y, launch_button.w, launch_button.h, launch, !details_active, status.is_connected, on_buttons && !details_active, false,
                                  [this]() { this->LaunchSelected(); });
        }
        if (details_button.visible) {
            this->AddActionButton(details_button.x, details_button.y, details_button.w, details_button.h, details_label, details_active, true, details_active, false,
                                  [this]() { this->OpenWindow(Window::Game); });
        }

        /* The new name and details fade in as the covers settle. */
        AddPaint(*this, [this, placed]() {
            if (const double progress = this->slide.Progress(); progress < 1.0) {
                const auto veil = color::WithAlpha(color::Ground, static_cast<u8>((1.0 - progress) * 255));
                for (const auto &part : placed) {
                    draw::Rect(part.x - 8, part.y - 4, part.w + 16, part.h + 8, veil);
                }
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
            this->hits.push_back({ x, y, GridCover, GridRowH, index });
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

    namespace {

        /* {{game.*}} and {{games.count}} in a theme's text. */
        std::string FillFields(std::string text, const library::Game *game, size_t count, const std::vector<usbl::DriveStatus> &drives) {
            const auto set = [&text](const char *name, const std::string &value) {
                const std::string field = std::string("{{") + name + "}}";
                for (size_t at = text.find(field); at != std::string::npos; at = text.find(field, at + value.size())) {
                    text.replace(at, field.size(), value);
                }
            };
            set("games.count", std::to_string(count));
            if (game != nullptr) {
                set("game.name", game->name);
                set("game.version", game->display_version);
                set("game.size", strings::Size(game->status.size));
                set("game.drive", DriveName(drives, game->status));
                set("game.playtime", PlayTime(game->play));
                set("game.developer", game->gametdb.developer);
                set("game.publisher", game->gametdb.publisher);
                set("game.year", game->gametdb.year);
                set("game.genre", game->gametdb.genres.empty() ? std::string() : strings::Genre(game->gametdb.genres.front()));
            }
            return text;
        }

        /* The console's clock as HH:MM. */
        std::string ClockText() {
            u64 now = 0;
            TimeCalendarTime calendar = {};
            TimeCalendarAdditionalInfo info = {};
            if (R_FAILED(timeGetCurrentTime(TimeType_Default, &now)) || R_FAILED(timeToCalendarTimeWithMyRule(now, &calendar, &info))) {
                return {};
            }
            char text[8];
            std::snprintf(text, sizeof(text), "%02u:%02u", calendar.hour, calendar.minute);
            return text;
        }

        std::string BatteryText() {
            u32 percent = 0;
            if (R_FAILED(psmInitialize())) {
                return {};
            }
            const bool ok = R_SUCCEEDED(psmGetBatteryChargePercentage(&percent));
            psmExit();
            return ok ? std::to_string(percent) + "%" : std::string();
        }

    }

    void LibraryScreen::AddFreeComponents() {
        const auto *game = this->GetSelectedGame();
        for (auto &component : components::FreeComponents("Home")) {
            const auto place = component.place;
            const pu::ui::Color ink = component.has_color ? component.color : color::Text;
            switch (component.kind) {
                case components::Kind::Clock:
                case components::Kind::Battery: {
                    /* Redrawn when its text changes (each minute for the clock), not with the screen. */
                    const bool clock = component.kind == components::Kind::Clock;
                    auto cache = std::make_shared<std::pair<std::string, pu::sdl2::TextureHandle::Ref>>();
                    auto last_check = std::make_shared<u64>(0);
                    AddPaint(*this, [place, ink, clock, cache, last_check]() {
                        const u64 now = armTicksToNs(armGetSystemTick()) / 1'000'000'000ull;
                        if (cache->second == nullptr || now - *last_check >= (clock ? 1 : 30)) {
                            *last_check = now;
                            if (const std::string text = clock ? ClockText() : BatteryText(); text != cache->first || cache->second == nullptr) {
                                cache->first = text;
                                cache->second = text.empty() ? nullptr : pu::sdl2::TextureHandle::New(pu::ui::render::RenderText(Font(place.font_size), text, ink));
                            }
                        }
                        if (cache->second != nullptr) {
                            s32 w = 0, h = 0;
                            SDL_QueryTexture(cache->second->Get(), nullptr, nullptr, &w, &h);
                            const double dw = w / Scale, dh = h / Scale;
                            const double x = place.align == Align::Center ? place.x + (place.w - dw) / 2 : (place.align == Align::Right ? place.x + place.w - dw : place.x);
                            draw::Image(cache->second->Get(), x, place.y + (place.h - dh) / 2, dw, dh);
                        }
                    });
                    break;
                }
                case components::Kind::Text:
                    AddText(*this, components::TextX(place), place.y, FillFields(component.text, game, this->games.size(), this->drives), place.font_size, ink, place.align, place.w);
                    break;
                case components::Kind::Image: {
                    /* Loaded once: the screen rebuilds on every button. */
                    auto &image = this->theme_images[component.image];
                    if (image == nullptr) {
                        if (SDL_Texture *texture = pu::ui::render::LoadImageFromFile(component.image); texture != nullptr) {
                            image = pu::sdl2::TextureHandle::New(texture);
                        }
                    }
                    if (image != nullptr) {
                        AddPaint(*this, [place, image]() { draw::Image(image->Get(), place.x, place.y, place.w, place.h); });
                    }
                    break;
                }
                case components::Kind::Cover:
                case components::Kind::Box:
                case components::Kind::Icon:
                    if (game != nullptr) {
                        const u64 id = game->status.application_id;
                        const auto kind = component.kind;
                        const s32 index = this->selected;
                        AddPaint(*this, [this, place, id, kind, index]() {
                            SDL_Texture *texture = kind == components::Kind::Icon ? nullptr : art::GetTexture(id, kind == components::Kind::Box ? art::Kind::Box : art::Kind::Cover);
                            if (texture == nullptr && kind != components::Kind::Icon) {
                                texture = art::GetTexture(id, art::Kind::Cover);
                            }
                            if (texture == nullptr) {
                                this->PaintCover(static_cast<size_t>(index), place.x, place.y, std::min(place.w, place.h), 1.0);
                                return;
                            }
                            /* Fitted inside the placement, keeping its shape. */
                            s32 tw = 0, th = 0;
                            SDL_QueryTexture(texture, nullptr, nullptr, &tw, &th);
                            const double k = std::min(place.w / std::max(1, tw), place.h / std::max(1, th));
                            const double w = tw * k, h = th * k;
                            draw::Image(texture, place.x + (place.w - w) / 2, place.y + (place.h - h) / 2, w, h);
                        });
                    }
                    break;
            }
        }
    }

    void LibraryScreen::AddFooter() {
        AddPaint(*this, []() {
            draw::Rect(0, FooterY - 1, DesignWidth, DesignHeight - FooterY + 1, color::Ground);
            draw::Rect(0, FooterY - 1, DesignWidth, 1, color::Track);
        });
        const auto counter = components::Place("Home", "counter", { true, Edge, FooterCenterY - 10, 300, 20, 14 });
        if (!this->games.empty() && counter.visible) {
            AddText(*this, components::TextX(counter), counter.y, strings::Format(Id::GameOf, { { "INDEX", std::to_string(this->selected + 1) }, { "COUNT", std::to_string(this->games.size()) } }), counter.font_size, color::Muted, counter.align, counter.w);
        }

        const bool carousel = this->view == View::Carousel;
        std::vector<Hint> hints = { { "-", strings::Get(Id::HintExit) }, { "+", strings::Get(Id::HintSettings) } };
        if (const auto views = this->AvailableViews(); views.size() > 1) {
            const auto current = std::find(views.begin(), views.end(), this->view);
            const s32 at = current != views.end() ? static_cast<s32>(current - views.begin()) : 0;
            hints.push_back({ "R", strings::Get(ViewLabels[static_cast<size_t>(views[Wrap(at + 1, static_cast<s32>(views.size()))])]) });
        }
        hints.push_back({ "Y", strings::Get(Id::HintDrives) });
        if (!this->browse.query.empty()) {
            hints.push_back({ "B", strings::Get(Id::HintClearSearch) });
        }
        if (!this->games.empty()) {
            hints.push_back({ "A", strings::Get(carousel && this->focus == Focus::Shelf ? Id::Launch : (carousel ? Id::HintSelect : Id::Details)) });
        }
        /* The hints end at the placement's right edge, centered on its height. */
        const auto bar = components::Place("Home", "hints", { true, Edge + 320, FooterY, DesignWidth - 2 * Edge - 320, DesignHeight - FooterY });
        if (bar.visible) {
            AddHintBar(*this, bar.x + bar.w, bar.y + bar.h / 2, hints, color::Muted);
        }
    }

    void LibraryScreen::AddActionButton(double x, double y, double w, double h, const std::string &label, bool active, bool enabled, bool ring, bool in_window,
                                        std::function<void()> on_tap) {
        if (on_tap) {
            this->taps.push_back({ x, y, w, h, std::move(on_tap) });
        }
        AddPaint(*this, [this, x, y, w, h, active, enabled, ring, in_window]() {
            draw::Translate rise(0, in_window ? this->WindowOffset() : 0.0);
            if (active) {
                if (!draw::Skinned(theme::Skin::ButtonActive, x, y, w, h, enabled ? 0xFF : 0x80)) {
                    draw::RoundRect(x, y, w, h, h / 2, enabled ? color::Primary : color::Raised);
                }
                draw::Circle(x + h / 2, y + h / 2, h * 0.31, enabled ? color::OnPrimary : color::Track);
            } else if (!draw::Skinned(theme::Skin::Button, x, y, w, h)) {
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
        /* A tap outside the window closes it; inside, only its buttons react. */
        this->taps.push_back({ 0, 0, DesignWidth, DesignHeight, [this]() { this->OnInput(HidNpadButton_B); } });
        this->taps.push_back({ x, y, w, h, []() {} });
        AddPaint(*this, [this, x, y, w, h]() {
            draw::Rect(0, 0, DesignWidth, DesignHeight, color::WithAlpha(color::Overlay, static_cast<u8>(color::Overlay.a * this->window_rise.Progress())));
            draw::Translate rise(0, this->WindowOffset());
            if (!draw::Skinned(theme::Skin::Panel, x, y, w, h)) {
                draw::RoundRect(x, y, w, h, 30, color::Panel);
            }
        });
    }

    void LibraryScreen::AddGameWindow(const library::Game &game) {
        namespace cmp = components;
        constexpr double Pad = 40;
        /* The window, the cover, the title, the details column and the buttons can each be placed by the theme
         * (Layout.Game.*); by default the column sits right of the cover and the buttons under it. */
        const double default_w = layout.window_w, default_h = layout.window_h;
        const auto window_place = cmp::Place("Game", "window", { true, (DesignWidth - default_w) / 2, std::max(24.0, (DesignHeight - 60 - default_h) / 2), default_w, default_h });
        const double W = window_place.w, H = window_place.h, X = window_place.x, Y = window_place.y;
        const auto cover_place = cmp::Place("Game", "cover", { true, X + Pad, Y + Pad, 300, 300 });
        const double CoverX = cover_place.x, CoverY = cover_place.y, CoverSize = std::min(cover_place.w, cover_place.h);
        const double DefaultColumnX = X + Pad + 300 + 44, DefaultColumnW = X + W - Pad - DefaultColumnX;
        const auto &status = game.status;
        this->AddWindowPanel(X, Y, W, H);

        /* Left: the cover, and what it is and where. */
        const s32 index = this->selected;
        const auto source = status.source;
        AddPaint(*this, [this, index, source, CoverX, CoverY, CoverSize, show_cover = cover_place.visible]() {
            if (!show_cover) {
                return;
            }
            draw::Translate rise(0, this->WindowOffset());
            const double k = 0.94 + 0.06 * this->window_rise.Progress();
            const double size = CoverSize * k, offset = (CoverSize - size) / 2;
            const u64 application_id = this->games[index].status.application_id;
            SDL_Texture *cover = art::GetTexture(application_id, art::Kind::Cover);
            SDL_Texture *spread = art::GetTexture(application_id, art::Kind::Spread);
            if (cover != nullptr || spread != nullptr) {
                /* GameTDB's cover, turned over (ZR) to its back: it narrows to nothing and opens on the other side. */
                const double t = this->flip.Progress();
                const bool back = t < 0.5 ? !this->show_back : this->show_back;
                const double squeeze = std::abs(std::cos(t * M_PI));
                const double h = size, w = h * CoverAspect * (this->flip.Progress() < 1.0 ? squeeze : 1.0);
                const double x = CoverX + (CoverSize - w) / 2, y = CoverY + offset + Float(3);
                if (back && spread != nullptr) {
                    draw::ImagePart(spread, art::SpreadBack(spread), x, y, w, h);
                } else if (cover != nullptr) {
                    draw::Image(cover, x, y, w, h);
                } else {
                    draw::ImagePart(spread, art::SpreadFront(spread), x, y, w, h);
                }
            } else {
                this->PaintCover(index, CoverX + offset, CoverY + offset + Float(3), size, 1.0);
            }
            /* The game card itself, peeking out from behind the cover's corner. */
            if (SDL_Texture *cart = art::GetTexture(application_id, art::Kind::Cart); cart != nullptr && source == usbl::GameSource::Xci) {
                draw::Image(cart, CoverX + CoverSize - 58, CoverY + CoverSize - 118 + Float(3), 80, 120);
            }
            SourceIcon(source, CoverX + 14, CoverY + CoverSize + 38, color::TextSoft);
        });
        if (cover_place.visible) {
            this->Slide(AddText(*this, CoverX + 36, CoverY + CoverSize + 18, FormatName(status), 15, color::TextSoft, Align::Left, CoverSize - 36));
            const std::string where = status.is_connected ? strings::Format(Id::OnDrive, { { "DRIVE", DriveName(this->drives, status) } }) : strings::Get(Id::PlugInDrive);
            this->Slide(AddText(*this, CoverX + 36, CoverY + CoverSize + 40, where, 15, status.is_connected ? color::Muted : color::Pending, Align::Left, CoverSize - 36));
        }

        /* Right: the name with its favorite star (Y), the details, what needs attention and the two actions. */
        const auto title_place = cmp::Place("Game", "title", { true, DefaultColumnX, Y + Pad - 4, DefaultColumnW, 52, 40 });
        double title_end = 0;
        double y = title_place.y + 4;
        if (title_place.visible) {
            const u32 big = title_place.font_size, small = std::max<u32>(12, big * 3 / 4);
            if (Fit(game.name, big, title_place.w - 48) == game.name) {
                const auto title = this->Slide(AddText(*this, title_place.x, y - 4, game.name, big, color::Text));
                title_end = title->GetWidth() / Scale;
                y += big + 12;
            } else {
                const auto lines = WrapLines(game.name, small, title_place.w - 48, 2);
                for (size_t line = 0; line < lines.size(); ++line) {
                    const auto text = this->Slide(AddText(*this, title_place.x, y + line * (small + 8), lines[line], small, color::Text));
                    title_end = text->GetWidth() / Scale;
                }
                y += (small + 8) * lines.size() + 10;
            }
            const double star_x = title_place.x + title_end + 28, star_y = y - (y - title_place.y) / 2 - 4;
            const bool favorite = game.is_favorite;
            AddPaint(*this, [this, star_x, star_y, favorite]() {
                draw::Translate rise(0, this->WindowOffset());
                draw::Star(star_x, star_y, 15, favorite ? color::Star : color::Track);
            });
        }
        /* The details column: GameTDB's line and chips, the rows and what needs attention. */
        const auto details_place = cmp::Place("Game", "details", { true, DefaultColumnX, y, DefaultColumnW, Y + H - Pad - 60 - 12 - y });
        const double ColumnX = details_place.x, ColumnW = details_place.w, ValueX = ColumnX + 170;
        y = details_place.y;

        /* What GameTDB knows: who made it and when, then genres, players, how it is played and its age rating. */
        if (game.in_gametdb && details_place.visible) {
            const auto &tdb = game.gametdb;
            std::string byline;
            for (const std::string *part : { &tdb.developer, &tdb.publisher, &tdb.year }) {
                if (!part->empty() && byline.find(*part) == std::string::npos) {
                    byline += (byline.empty() ? "" : "  ·  ") + *part;
                }
            }
            if (!byline.empty()) {
                this->Slide(AddText(*this, ColumnX, y - 6, byline, 15, color::Muted, Align::Left, ColumnW));
                y += 24;
            }
            std::vector<std::string> chips;
            for (size_t i = 0; i < std::min<size_t>(tdb.genres.size(), 2); ++i) {
                chips.push_back(strings::Genre(tdb.genres[i]));
            }
            if (!tdb.players.empty()) {
                chips.push_back(tdb.players == "1" ? strings::Get(Id::PlayersOne) : strings::Format(Id::PlayersMany, { { "COUNT", tdb.players } }));
            }
            std::string modes;
            for (const auto &[on, id] : { std::pair{ tdb.tv, Id::ModeTv }, std::pair{ tdb.handheld, Id::ModeHandheld }, std::pair{ tdb.tabletop, Id::ModeTabletop } }) {
                if (on) {
                    modes += (modes.empty() ? "" : " · ") + strings::Get(id);
                }
            }
            if (!modes.empty()) {
                chips.push_back(modes);
            }
            if (const std::string rating = AgeRating(tdb); !rating.empty()) {
                chips.push_back(rating);
            }
            std::vector<std::pair<double, double>> boxes;
            double chip_x = ColumnX;
            for (const auto &chip : chips) {
                const double w = TextWidth(chip, 13) + 20;
                if (chip_x + w > ColumnX + ColumnW) {
                    break;
                }
                boxes.push_back({ chip_x, w });
                chip_x += w + 6;
            }
            const double chip_y = y;
            AddPaint(*this, [this, boxes, chip_y]() {
                draw::Translate rise(0, this->WindowOffset());
                for (const auto &[x, w] : boxes) {
                    if (!draw::Skinned(theme::Skin::Chip, x, chip_y, w, 26)) {
                        draw::RoundRect(x, chip_y, w, 26, 8, color::Raised);
                    }
                }
            });
            for (size_t i = 0; i < boxes.size(); ++i) {
                this->Slide(AddText(*this, boxes[i].first + 10, chip_y + 5, chips[i], 13, color::TextSoft));
            }
            if (!boxes.empty()) {
                y += 34;
            }
        }

        std::vector<std::pair<Id, std::string>> rows;
        rows.push_back({ Id::RowVersion, game.display_version.empty() ? "—" : game.display_version });
        const Id update_state = status.has_update ? Id::UpdateInstalled : (status.has_parts_elsewhere ? Id::UpdateElsewhereRow : (game.update_missing ? Id::UpdateMissingRow : Id::UpdateNone));
        rows.push_back({ Id::RowUpdate, strings::Get(update_state) });
        rows.push_back({ Id::RowAddOns, std::to_string(status.add_on_count) });
        rows.push_back({ Id::RowSize, strings::Size(status.size) });
        if (status.is_connected) {
            rows.push_back({ Id::RowFile, Fragmentation(status) });
        }
        /* Play time and the last session share a row (the window has room for one more line of GameTDB). */
        rows.push_back({ Id::RowPlayTime, game.play.last_played != 0 ? PlayTime(game.play) + "  ·  " + FormatDate(game.play.last_played) : PlayTime(game.play) });
        y += 14;
        if (!details_place.visible) {
            rows.clear();
        }
        for (const auto &[label, value] : rows) {
            this->Slide(AddText(*this, ColumnX, y, strings::Get(label), 16, color::Muted, Align::Left, 160));
            const bool warn = (label == Id::RowFile && status.fragments != 1) || (label == Id::RowUpdate && !status.has_update && (game.update_missing || status.has_parts_elsewhere));
            this->Slide(AddText(*this, ValueX, y, value, 16, warn ? color::Alert : color::Text, Align::Left, ColumnW - 170));
            y += 27;
        }

        if (const auto [alert, is_alert] = GameAlert(game, true); !alert.empty() && details_place.visible) {
            const auto lines = WrapLines(alert, 14, ColumnW - 60, 3);
            const double box_y = y + 6, box_h = 24 + lines.size() * 20;
            const pu::ui::Color surface = is_alert ? color::AlertSurface : color::Raised;
            const pu::ui::Color icon = is_alert ? color::Alert : color::Muted;
            AddPaint(*this, [this, box_y, box_h, surface, icon, ColumnX, ColumnW]() {
                draw::Translate rise(0, this->WindowOffset());
                draw::RoundRect(ColumnX, box_y, ColumnW, box_h, 14, surface);
                draw::IconWarning(ColumnX + 25, box_y + 22, 18, icon);
            });
            for (size_t line = 0; line < lines.size(); ++line) {
                this->Slide(AddText(*this, ColumnX + 44, box_y + 12 + line * 20, lines[line], 14, is_alert ? color::AlertText : color::TextSoft));
            }
        }

        /* The two actions; the focused one is filled, carries the A and the focus ring. */
        const std::string launch = strings::Get(Id::Launch);
        const std::string hide = strings::Get(status.is_hidden ? Id::ShowOnHome : Id::HideFromHome);
        const double launch_w = TextWidth(launch, 20) + 96, hide_w = std::min(DefaultColumnW - launch_w - 14, TextWidth(hide, 20) + 96);
        const auto launch_place = cmp::Place("Game", "button.launch", { true, DefaultColumnX, Y + H - Pad - 60, launch_w, 60 });
        const auto hide_place = cmp::Place("Game", "button.hide", { true, launch_place.x + launch_place.w + 14, launch_place.y, hide_w, 60 });
        const bool focus_launch = this->game_button == GameButton::Launch;
        if (launch_place.visible) {
            this->AddActionButton(launch_place.x, launch_place.y, launch_place.w, launch_place.h, launch, focus_launch, status.is_connected, focus_launch, true, [this]() {
                this->game_button = GameButton::Launch;
                this->OnInput(HidNpadButton_A);
            });
        }
        if (hide_place.visible) {
            this->AddActionButton(hide_place.x, hide_place.y, hide_place.w, hide_place.h, hide, !focus_launch, true, !focus_launch, true, [this]() {
                this->game_button = GameButton::ToggleHidden;
                this->OnInput(HidNpadButton_A);
            });
        }

        std::vector<Hint> hints = { { "B", strings::Get(Id::HintBack) } };
        if (this->games.size() > 1) {
            hints.push_back({ "L R", strings::Get(Id::HintSwitchGame) });
        }
        hints.push_back({ "Y", strings::Get(game.is_favorite ? Id::HintUnfavorite : Id::HintFavorite) });
        if (art::GetTexture(game.status.application_id, art::Kind::Spread) != nullptr) {
            hints.push_back({ "ZR", strings::Get(Id::HintFlipCover) });
        }
        AddHintBar(*this, DesignWidth - Edge, FooterCenterY, hints, color::TextSoft);
    }

    void LibraryScreen::AddDrivesWindow() {
        if (this->show_stats) {
            this->AddStatsWindow();
            return;
        }
        namespace cmp = components;
        constexpr double Pad = 40, CardGap = 12;
        /* Name and space; a row of tags; the usage bar once the free space is known; the warning when almost full. */
        const auto card_height = [](const usbl::DriveStatus &drive) {
            return (drive.free_space != 0 ? 124.0 : 96.0) + (DriveUsage(drive) >= 0.9 ? 28.0 : 0.0);
        };
        usbl::ConfigValue homebrew_access = {};
        const bool show_homebrew_path = R_SUCCEEDED(usbl::GetConfigValue(&homebrew_access, usbl::ConfigKey::HomebrewAccess)) && homebrew_access.enabled;
        double cards_h = 0;
        for (const auto &drive : this->drives) {
            cards_h += card_height(drive) + CardGap;
        }
        const double default_h = 36 + 100 + std::max(cards_h - CardGap, 60.0) + Pad;
        /* The theme can place the window, its title and the cards (Layout.Drives.*). */
        const auto window_place = cmp::Place("Drives", "window", { true, 200, std::max(24.0, (FooterY - default_h) / 2), 880, default_h });
        const double X = window_place.x, W = window_place.w, y = window_place.y, h = window_place.h;
        this->AddWindowPanel(X, y, W, h);
        const auto cards_place = cmp::Place("Drives", "cards", { true, X + Pad, y + 136, W - 2 * Pad, h - 136 - Pad });
        const double CardX = cards_place.x, CardW = cards_place.w;

        const auto title_place = cmp::Place("Drives", "title", { true, X + Pad, y + 30, W - 2 * Pad - 200, 40, 30 });
        if (title_place.visible) {
            this->Slide(AddText(*this, cmp::TextX(title_place), title_place.y, strings::Get(Id::DevicesTitle), title_place.font_size, color::Text, title_place.align, title_place.w));
        }
        this->Slide(AddText(*this, X + W - Pad, y + 40, "L R  " + strings::Get(Id::TabStats), 15, color::Muted, Align::Right));
        this->drive_selected = std::clamp<s32>(this->drive_selected, 0, std::max<s32>(0, static_cast<s32>(this->drives.size()) - 1));
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
            const auto total_place = cmp::Place("Drives", "total", { true, X + Pad, y + 76, W - 2 * Pad, 20, 15 });
            if (total_place.visible) {
                this->Slide(AddText(*this, cmp::TextX(total_place), total_place.y, total, total_place.font_size, color::Muted, total_place.align, total_place.w));
            }
        }

        double card_y = cards_place.y;
        if (this->drives.empty()) {
            this->Slide(AddText(*this, X + W / 2, card_y + 18, strings::Get(Id::NoDrives), 19, color::MutedSoft, Align::Center, CardW));
        }
        for (size_t drive_index = 0; drive_index < this->drives.size(); ++drive_index) {
            const auto &drive = this->drives[drive_index];
            const double usage = DriveUsage(drive), card_h = card_height(drive);
            if (this->drives.size() > 1) {
                const s32 tapped = static_cast<s32>(drive_index);
                this->taps.push_back({ CardX, card_y, CardW, card_h, [this, tapped]() {
                    this->drive_selected = tapped;
                    this->OnInput(HidNpadButton_A);
                } });
            }
            if (static_cast<s32>(drive_index) == this->drive_selected && this->drives.size() > 1) {
                AddPaint(*this, [=, this]() {
                    draw::Translate rise(0, this->WindowOffset());
                    draw::FocusRing(CardX, card_y, CardW, card_h, 20, 4);
                });
            }
            const bool almost_full = usage >= 0.9;
            const bool has_space = drive.free_space != 0;
            const double BarX = CardX + 24, PercentW = 56;
            const double BarW = CardW - 48 - PercentW;
            AddPaint(*this, [=, this]() {
                draw::Translate rise(0, this->WindowOffset());
                if (!draw::Skinned(theme::Skin::Card, CardX, card_y, CardW, card_h)) {
                    draw::RoundRect(CardX, card_y, CardW, card_h, 20, color::Raised);
                }
                draw::RoundRect(CardX + 24, card_y + 20, 48, 48, 14, color::Ground);
                draw::IconUsbStick(CardX + 48, card_y + 44, 26, color::TextSoft);
                if (has_space) {
                    draw::UsageBar(BarX, card_y + 94, BarW, 12, usage);
                }
            });
            const std::string space = has_space                    ? strings::Format(Id::FreeOf, { { "FREE", strings::Size(drive.free_space) }, { "TOTAL", strings::Size(drive.capacity) } })
                                    : drive.is_counting_free_space ? strings::Get(Id::CountingSpace)
                                                                   : std::string();
            const double space_w = space.empty() ? 0 : TextWidth(space, 17) + 24;
            this->Slide(AddText(*this, CardX + 88, card_y + 18, drive.name, 19, color::Text, Align::Left, CardW - 112 - space_w));
            if (!space.empty()) {
                this->Slide(AddText(*this, CardX + CardW - 24, card_y + 20, space, 17, has_space ? color::Text : color::Muted, Align::Right));
            }
            if (has_space) {
                this->Slide(AddText(*this, CardX + CardW - 24, card_y + 89, std::to_string(static_cast<s32>(std::lround(usage * 100))) + "%", 14, almost_full ? color::ErrorText : color::Text, Align::Right));
            }

            /* One tag per fact, in place of a sentence: what is on it first, then where homebrew read it. */
            struct Tag {
                std::string text;
                pu::ui::Color fill;
                pu::ui::Color ink;
            };
            std::vector<Tag> tags;
            const auto fact  = [&](std::string text) { tags.push_back({ std::move(text), color::Ground, color::TextSoft }); };
            const auto alert = [&](std::string text) { tags.push_back({ std::move(text), color::AlertSurface, color::AlertText }); };
            switch (drive.state) {
                case usbl::DriveState::Reading:
                    tags.push_back({ strings::Get(Id::DriveReading), color::Ground, color::Info });
                    break;
                case usbl::DriveState::Unreadable:
                    alert(strings::Get(Id::DriveUnreadable));
                    break;
                case usbl::DriveState::NoGamesFolder:
                    fact(drive.is_exfat ? "exFAT" : "FAT32");
                    alert(strings::Get(Id::DriveNoGamesFolder));
                    break;
                case usbl::DriveState::Ready:
                    fact(drive.is_exfat ? "exFAT" : "FAT32");
                    if (drive.game_count + drive.update_count + drive.add_on_count == 0) {
                        tags.push_back({ strings::Get(Id::DriveNoGames), color::Ground, color::Muted });
                    }
                    if (drive.game_count != 0) {
                        fact(strings::Count(drive.game_count, Id::GameOne, Id::GameMany));
                    }
                    if (drive.update_count != 0) {
                        fact(strings::Count(drive.update_count, Id::UpdateOne, Id::UpdateMany));
                    }
                    if (drive.add_on_count != 0) {
                        fact(strings::Count(drive.add_on_count, Id::AddOnOne, Id::AddOnMany));
                    }
                    break;
            }
            /* Protocol 1.1; 0 from an older module, and only while homebrew_access is on. */
            if (drive.volume_serial != 0 && drive.state != usbl::DriveState::Reading && show_homebrew_path) {
                char path[40];
                std::snprintf(path, sizeof(path), "/usbloadernx/usb/%04X-%04X", static_cast<unsigned>(drive.volume_serial >> 16), static_cast<unsigned>(drive.volume_serial & 0xFFFF));
                tags.push_back({ strings::Format(Id::DriveHomebrewPath, { { "PATH", path } }), color::Ground, color::Info });
            }

            constexpr double TagH = 28, TagPad = 12, TagGap = 8;
            const double TagRight = CardX + CardW - 24;
            double tag_x = CardX + 88;
            const double tag_y = card_y + 50;
            for (const auto &tag : tags) {
                const double tag_w = std::min(TextWidth(tag.text, 14) + 2 * TagPad, TagRight - tag_x);
                if (tag_w < 2 * TagPad + 24) {
                    break;
                }
                AddPaint(*this, [=, this, fill = tag.fill]() {
                    draw::Translate rise(0, this->WindowOffset());
                    draw::RoundRect(tag_x, tag_y, tag_w, TagH, TagH / 2, fill);
                });
                this->Slide(AddText(*this, tag_x + TagPad, tag_y + 5, tag.text, 14, tag.ink, Align::Left, tag_w - 2 * TagPad));
                tag_x += tag_w + TagGap;
            }
            if (almost_full) {
                AddPaint(*this, [=, this]() {
                    draw::Translate rise(0, this->WindowOffset());
                    draw::IconWarning(CardX + 32, card_y + 140, 16, color::ErrorText);
                });
                this->Slide(AddText(*this, CardX + 48, card_y + 130, strings::Get(Id::AlmostFull), 14, color::ErrorText, Align::Left, CardW - 72));
            }
            card_y += card_h + CardGap;
        }

        std::vector<Hint> hints = { { "B", strings::Get(Id::HintBack) } };
        if (this->drives.size() > 1) {
            hints.push_back({ "A", strings::Get(Id::HintDriveGames) });
        }
        AddHintBar(*this, DesignWidth - Edge, FooterCenterY, hints, color::TextSoft);
    }

    void LibraryScreen::AddStatsWindow() {
        constexpr double X = 200, W = 880, Pad = 40, H = 560, ColumnW = (W - 3 * Pad) / 2;
        const double y = std::max(24.0, (FooterY - H) / 2);
        this->AddWindowPanel(X, y, W, H);
        this->Slide(AddText(*this, X + Pad, y + 30, strings::Get(Id::TabStats), 30, color::Text));
        this->Slide(AddText(*this, X + W - Pad, y + 40, "L R  " + strings::Get(Id::DevicesTitle), 15, color::Muted, Align::Right));

        /* The whole library in one line, whatever the filter: games, space and time played. */
        const auto &all = this->all_games;
        u64 size = 0, play_ns = 0;
        for (const auto &game : all) {
            size += game.status.size;
            play_ns += game.play.play_time_ns;
        }
        library::PlayStats total = { play_ns, 1, 0 };
        const std::string summary = strings::Format(Id::StatsSummary, { { "GAMES", strings::Count(all.size(), Id::GameOne, Id::GameMany) }, { "SIZE", strings::Size(size) }, { "TIME", PlayTime(total) } });
        this->Slide(AddText(*this, X + Pad, y + 80, summary, 19, color::Primary, Align::Left, W - 2 * Pad));

        /* Two columns of five: most played (by time) and largest (by space), each with a bar against the first. */
        const auto column = [&](double cx, Id title, std::vector<const library::Game *> ranked, bool by_time) {
            this->Slide(AddText(*this, cx, y + 156, strings::Get(title), 17, color::Text));
            if (ranked.empty()) {
                this->Slide(AddText(*this, cx, y + 196, strings::Get(Id::StatsNonePlayed), 15, color::Muted, Align::Left, ColumnW));
                return;
            }
            const double top_value = by_time ? static_cast<double>(ranked.front()->play.play_time_ns) : static_cast<double>(ranked.front()->status.size);
            for (size_t i = 0; i < ranked.size(); ++i) {
                const auto &game = *ranked[i];
                const double row_y = y + 196 + i * 62;
                const double value = by_time ? static_cast<double>(game.play.play_time_ns) : static_cast<double>(game.status.size);
                const double share = top_value > 0 ? value / top_value : 0;
                const std::string amount = by_time ? PlayTime(game.play) : strings::Size(game.status.size);
                const double amount_w = TextWidth(amount, 14) + 12;
                this->Slide(AddText(*this, cx, row_y, game.name, 16, color::TextSoft, Align::Left, ColumnW - amount_w));
                this->Slide(AddText(*this, cx + ColumnW, row_y + 2, amount, 14, color::Muted, Align::Right));
                AddPaint(*this, [this, cx, row_y, share]() {
                    draw::Translate rise(0, this->WindowOffset());
                    draw::RoundRect(cx, row_y + 30, ColumnW, 8, 4, color::Track);
                    draw::RoundRect(cx, row_y + 30, std::max(8.0, ColumnW * share), 8, 4, color::Primary);
                });
            }
        };
        std::vector<const library::Game *> played, largest;
        for (const auto &game : all) {
            if (game.play.play_time_ns > 0) {
                played.push_back(&game);
            }
            largest.push_back(&game);
        }
        constexpr size_t Top = 5;
        const auto keep = [](std::vector<const library::Game *> *list, auto better) {
            std::stable_sort(list->begin(), list->end(), better);
            if (list->size() > Top) {
                list->resize(Top);
            }
        };
        keep(&played, [](const library::Game *a, const library::Game *b) { return a->play.play_time_ns > b->play.play_time_ns; });
        keep(&largest, [](const library::Game *a, const library::Game *b) { return a->status.size > b->status.size; });
        column(X + Pad, Id::StatsMostPlayed, played, true);
        column(X + 2 * Pad + ColumnW, Id::StatsLargest, largest, false);

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
            this->AddActionButton(close_x, button_y, close_w, 56, close, !on_action, true, !on_action, true, [this]() { this->OnInput(HidNpadButton_B); });
            this->AddActionButton(action_x, button_y, action_w, 56, action, on_action, true, on_action, true, [this]() {
                this->message_on_action = true;
                this->OnInput(HidNpadButton_A);
            });
            return;
        }

        const std::string ok = strings::Get(Id::Ok);
        const double button_w = TextWidth(ok, 18) + 88, button_x = X + W - Pad - button_w, button_y = y + h - Pad - 56;
        this->taps.push_back({ button_x, button_y, button_w, 56, [this]() { this->OnInput(HidNpadButton_A); } });
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
        if (this->view == View::Carousel || this->view == View::Boxes) {
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
        this->show_back = false;
        this->show_stats = false;
        sound::Play(sound::Effect::Open);
        if (window == Window::Game && this->GetSelectedGame() != nullptr) {
            this->game_button = this->GetSelectedGame()->status.is_connected ? GameButton::Launch : GameButton::ToggleHidden;
        }
        this->Rebuild();
    }

    void LibraryScreen::OnTouch(const pu::ui::TouchPoint &touch) {
        const auto event = this->gesture.Update(touch);
        if (event.kind == TouchGesture::Event::Kind::None) {
            return;
        }
        if (event.kind == TouchGesture::Event::Kind::Swipe) {
            if (this->window == Window::Game && std::abs(event.x) > std::abs(event.y)) {
                /* In a game's window, a swipe goes to the previous or next game, like L and R. */
                this->OnInput(event.x < 0 ? HidNpadButton_R : HidNpadButton_L);
            } else if (this->window == Window::None && std::abs(event.x) >= std::abs(event.y)) {
                /* Dragging the games left brings the next ones, about one game per 160 px. */
                const s32 steps = std::clamp(static_cast<s32>(std::abs(event.x) / 160) + 1, 1, 5);
                for (s32 i = 0; i < steps; ++i) {
                    this->OnInput(event.x < 0 ? HidNpadButton_Right : HidNpadButton_Left);
                }
            } else if (this->window == Window::None) {
                this->OnInput(event.y < 0 ? HidNpadButton_Down : HidNpadButton_Up);
            }
            return;
        }
        if (Tap(this->taps, event.x, event.y) || this->window != Window::None) {
            return;
        }
        /* A tap on a game: on the selected one it does what A does; on another, it picks it. */
        for (const auto &hit : this->hits) {
            if (event.x >= hit.x && event.x < hit.x + hit.w && event.y >= hit.y && event.y < hit.y + hit.h) {
                if (hit.index == this->selected) {
                    this->focus = Focus::Shelf;
                    this->OnInput(HidNpadButton_A);
                } else if (this->view == View::Carousel || this->view == View::Boxes) {
                    const s32 count = static_cast<s32>(this->games.size());
                    s32 delta = Wrap(hit.index - this->selected, count);
                    if (delta > count / 2) {
                        delta -= count;
                    }
                    this->Move(delta);
                } else {
                    sound::Play(sound::Effect::Move);
                    this->selected = hit.index;
                    this->focus_glide.Start();
                    this->Rebuild();
                }
                return;
            }
        }
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
                    this->show_back = false;
                    this->window_rise.Start();
                    this->Rebuild();
                } else if ((keys_down & HidNpadButton_Y) && game != nullptr) {
                    this->actions.toggle_favorite(*game);
                } else if ((keys_down & HidNpadButton_ZR) && game != nullptr && art::GetTexture(game->status.application_id, art::Kind::Spread) != nullptr) {
                    sound::Play(sound::Effect::Whoosh);
                    this->show_back = !this->show_back;
                    this->flip.Start();
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
                if (keys_down & (HidNpadButton_B | HidNpadButton_Y)) {
                    sound::Play(sound::Effect::Back);
                    this->window = Window::None;
                    this->Rebuild();
                } else if (keys_down & (HidNpadButton_L | HidNpadButton_R)) {
                    sound::Play(sound::Effect::Whoosh);
                    this->show_stats = !this->show_stats;
                    this->window_rise.Start();
                    this->Rebuild();
                } else if (!this->show_stats && this->drives.size() > 1 && (keys_down & (HidNpadButton_AnyUp | HidNpadButton_AnyDown))) {
                    const s32 next = std::clamp<s32>(this->drive_selected + ((keys_down & HidNpadButton_AnyUp) ? -1 : 1), 0, static_cast<s32>(this->drives.size()) - 1);
                    if (next != this->drive_selected) {
                        sound::Play(sound::Effect::Move);
                        this->drive_selected = next;
                        this->Rebuild();
                    }
                } else if (!this->show_stats && this->drives.size() > 1 && (keys_down & HidNpadButton_A)) {
                    sound::Play(sound::Effect::Select);
                    this->window = Window::None;
                    this->actions.show_drive(this->drives[this->drive_selected].drive);
                } else if (keys_down & HidNpadButton_A) {
                    sound::Play(sound::Effect::Back);
                    this->window = Window::None;
                    this->Rebuild();
                }
                return;
            case Window::None:
                break;
        }

        const bool carousel = this->view == View::Carousel;
        if ((keys_down & (HidNpadButton_AnyUp | HidNpadButton_AnyDown)) && this->view == View::Boxes) {
            return;
        }
        if (keys_down & HidNpadButton_Minus) {
            this->actions.exit();
        } else if (keys_down & HidNpadButton_Plus) {
            this->actions.open_settings();
        } else if (keys_down & HidNpadButton_X) {
            this->actions.next_sort();
        } else if (keys_down & HidNpadButton_ZL) {
            this->actions.search();
        } else if (keys_down & HidNpadButton_ZR) {
            this->actions.next_filter();
        } else if ((keys_down & HidNpadButton_B) && !this->browse.query.empty()) {
            this->actions.clear_search();
        } else if ((keys_down & HidNpadButton_B) && this->focus == Focus::Buttons) {
            this->MoveFocus(Focus::Shelf);
        } else if (keys_down & HidNpadButton_Y) {
            this->OpenWindow(Window::Drives);
        } else if (keys_down & (HidNpadButton_R | HidNpadButton_L)) {
            this->SwitchView((keys_down & HidNpadButton_L) ? -1 : 1);
        } else if (this->view == View::Shelves && !this->shelves.empty()) {
            const auto [shelf, place] = this->ShelfPosition();
            s32 next_shelf = shelf, next_place = place;
            if (keys_down & HidNpadButton_AnyLeft) {
                --next_place;
            } else if (keys_down & HidNpadButton_AnyRight) {
                ++next_place;
            } else if (keys_down & HidNpadButton_AnyUp) {
                --next_shelf;
            } else if (keys_down & HidNpadButton_AnyDown) {
                ++next_shelf;
            } else if ((keys_down & HidNpadButton_A) && this->GetSelectedGame() != nullptr) {
                this->OpenWindow(Window::Game);
                return;
            }
            if (next_shelf < 0 || next_shelf >= static_cast<s32>(this->shelves.size())) {
                return;
            }
            const auto &games = this->shelves[next_shelf].games;
            next_place = next_shelf != shelf ? std::min<s32>(next_place, static_cast<s32>(games.size()) - 1) : next_place;
            if (next_place < 0 || next_place >= static_cast<s32>(games.size()) || (next_shelf == shelf && next_place == place)) {
                return;
            }
            if (next_shelf != shelf) {
                this->first_spine = 0;
                sound::Play(sound::Effect::Whoosh);
            } else {
                sound::Play(sound::Effect::Move);
            }
            this->selected = games[next_place];
            this->focus_glide.Start();
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
            this->Move(-this->Columns());
        } else if (keys_down & HidNpadButton_AnyDown) {
            if (!carousel) {
                this->Move(this->Columns());
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
