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
#include <ui/theme.hpp>
#include <ui/animated_image.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <iterator>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>
#include <sys/stat.h>
#include <minizip/unzip.h>
#include <json.hpp>
#include <manager_settings.hpp>
#include <sd_files.hpp>
#include <strings.hpp>
#include <ui/motion.hpp>

namespace ui::theme {

    namespace {

        const std::string ThemesDirectory = sd::Path(usbl::paths::ThemesDir);
        constexpr const char ThemeKey[] = "theme";

        struct Slot {
            const char *name;
            pu::ui::Color *color;
            pu::ui::Color default_value;
        };

        /* Every palette entry a theme can set, with the built-in value to go back to. */
        Slot g_slots[] = {
            { "Ground",    &color::Ground,    color::Ground },
            { "Panel",     &color::Panel,     color::Panel },
            { "Raised",    &color::Raised,    color::Raised },
            { "Track",     &color::Track,     color::Track },
            { "Text",      &color::Text,      color::Text },
            { "TextSoft",  &color::TextSoft,  color::TextSoft },
            { "Muted",     &color::Muted,     color::Muted },
            { "MutedSoft", &color::MutedSoft, color::MutedSoft },
            { "Faint",     &color::Faint,     color::Faint },
            { "Primary",   &color::Primary,   color::Primary },
            { "Info",      &color::Info,      color::Info },
            { "Warning",   &color::Warning,   color::Warning },
            { "Pending",   &color::Pending,   color::Pending },
            { "Error",     &color::Error,     color::Error },
            { "ErrorText", &color::ErrorText, color::ErrorText },
            { "OnPrimary", &color::OnPrimary, color::OnPrimary },
            { "Focus",     &color::Focus,     color::Focus },
            { "FocusAlt",  &color::FocusAlt,  color::FocusAlt },
            { "Star",      &color::Star,      color::Star },
            { "Alert",        &color::Alert,        color::Alert },
            { "AlertSurface", &color::AlertSurface, color::AlertSurface },
            { "AlertText",    &color::AlertText,    color::AlertText },
            { "Overlay",   &color::Overlay,   color::Overlay },
        };

        std::string g_current;
        std::string g_display_name;
        pu::sdl2::TextureHandle::Ref g_background;
        /* A GIF or WebP with more than one frame plays in place of g_background. */
        std::unique_ptr<AnimatedImage> g_animated_background;
        bool g_background_tiled = false;
        double g_background_dim = 0.4;
        bool g_particles = true;
        std::string g_music;

        /* A theme's font file, read once and shared by its sizes; kept while the Manager runs, since the renderer
         * cannot drop a font it was given. */
        struct ThemeFont {
            std::string path;
            std::unique_ptr<u8[]> data;
            size_t size = 0;
            double scale = 1.0;
            std::vector<u32> registered;
        };
        std::vector<std::unique_ptr<ThemeFont>> g_fonts;

        constexpr const char *SkinNames[] = { "Panel", "Card", "CardFocused", "Chip", "Button", "ButtonActive", "FocusRing" };
        static_assert(std::size(SkinNames) == static_cast<size_t>(Skin::Count));
        constexpr const char *IconNames[] = { "Usb", "Clock", "Warning", "Chevron", "Star", "Package", "Grid" };
        static_assert(std::size(IconNames) == static_cast<size_t>(Icon::Count));

        struct LoadedSkin {
            pu::sdl2::TextureHandle::Ref handle;
            SkinImage image;
        };
        LoadedSkin g_skins[static_cast<size_t>(Skin::Count)];
        pu::sdl2::TextureHandle::Ref g_icons[static_cast<size_t>(Icon::Count)];
        bool g_tint_icons = false;
        std::vector<std::string> g_views;
        /* Names, sizes and times of the applied theme's files (and of sfx/), to notice an edit. */
        std::string g_signature;
        /* Screens whose components a theme can place; anything else under Layout. is a whole-screen setting. */
        constexpr const char *ComponentScreens[] = { "Home", "Game", "Drives", "Settings" };
        std::map<std::string, std::map<std::string, ComponentProperties>> g_components;
        bool g_game_backdrop = true;
        constexpr char GameBackdropKey[] = "game_backdrop";
        std::string g_default_view;
        ThemeFont *g_font = nullptr;
        constexpr long MaxFontFile = 16 * 1024 * 1024;

        ThemeFont *LoadFont(const std::string &path, double scale) {
            for (auto &font : g_fonts) {
                if (font->path == path && font->scale == scale) {
                    return font.get();
                }
            }
            FILE *file = std::fopen(path.c_str(), "rb");
            if (file == nullptr) {
                return nullptr;
            }
            std::fseek(file, 0, SEEK_END);
            const long size = std::ftell(file);
            std::rewind(file);
            auto font = std::make_unique<ThemeFont>();
            if (size > 0 && size <= MaxFontFile) {
                font->data = std::make_unique<u8[]>(static_cast<size_t>(size));
                font->size = std::fread(font->data.get(), 1, static_cast<size_t>(size), file) == static_cast<size_t>(size) ? static_cast<size_t>(size) : 0;
            }
            std::fclose(file);
            if (font->size == 0) {
                return nullptr;
            }
            /* Not a font FreeType can open: keep the console's. */
            if (TTF_Font *probe = TTF_OpenFontRW(SDL_RWFromConstMem(font->data.get(), static_cast<int>(font->size)), 1, 20); probe != nullptr) {
                TTF_CloseFont(probe);
            } else {
                return nullptr;
            }
            font->path = path;
            font->scale = scale;
            g_fonts.push_back(std::move(font));
            return g_fonts.back().get();
        }

        std::string ThemeFontName(const ThemeFont &font, u32 px) {
            return "theme:" + font.path + ":" + std::to_string(font.scale) + ":" + std::to_string(px);
        }

        int HexDigit(char c) {
            return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
        }

        /* "#RRGGBB" or "#RRGGBBAA". */
        bool ParseColor(const std::string &text, pu::ui::Color *out) {
            if ((text.size() != 7 && text.size() != 9) || text[0] != '#') {
                return false;
            }
            u8 channels[4] = { 0, 0, 0, 0xFF };
            for (size_t i = 0; i < (text.size() - 1) / 2; ++i) {
                const int high = HexDigit(text[1 + i * 2]), low = HexDigit(text[2 + i * 2]);
                if (high < 0 || low < 0) {
                    return false;
                }
                channels[i] = static_cast<u8>(high * 16 + low);
            }
            *out = { channels[0], channels[1], channels[2], channels[3] };
            return true;
        }

        std::string ThemePath(const std::string &name) {
            return ThemesDirectory + "/" + name;
        }

        bool Exists(const std::string &path) {
            struct stat info;
            return stat(path.c_str(), &info) == 0;
        }

    }

    namespace {

        constexpr size_t MaxThemeFileSize = 64 * 1024 * 1024;

        bool EndsWithZip(const std::string &name) {
            if (name.size() <= 4) {
                return false;
            }
            std::string ext = name.substr(name.size() - 4);
            for (char &c : ext) {
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            }
            return ext == ".zip";
        }

        s64 ModifiedTime(const std::string &path) {
            struct stat info;
            return stat(path.c_str(), &info) == 0 ? static_cast<s64>(info.st_mtime) : -1;
        }

        /* Names inside the archive must stay inside the theme's folder. */
        bool IsSafeRelativePath(const std::string &path) {
            if (path.empty() || path[0] == '/' || path[0] == '\\' || path.find(':') != std::string::npos) {
                return false;
            }
            size_t start = 0;
            while (start <= path.size()) {
                const size_t end = path.find_first_of("/\\", start);
                const std::string part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (part == "..") {
                    return false;
                }
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
            return true;
        }

        std::string CurrentEntryName(unzFile zip) {
            char name[512] = {};
            unz_file_info64 info = {};
            if (unzGetCurrentFileInfo64(zip, &info, name, sizeof(name), nullptr, 0, nullptr, 0) != UNZ_OK) {
                return {};
            }
            return name;
        }

        /* theme.json at the archive's root, or inside one folder at its root (zipping the theme's folder itself). */
        bool FindThemeRoot(unzFile zip, std::string *out_prefix) {
            for (int rc = unzGoToFirstFile(zip); rc == UNZ_OK; rc = unzGoToNextFile(zip)) {
                const std::string name = CurrentEntryName(zip);
                if (name == "theme.json") {
                    *out_prefix = {};
                    return true;
                }
                if (const size_t slash = name.find('/'); slash != std::string::npos && name.compare(slash + 1, std::string::npos, "theme.json") == 0) {
                    *out_prefix = name.substr(0, slash + 1);
                    return true;
                }
            }
            return false;
        }

        bool ExtractCurrentFile(unzFile zip, const std::string &path) {
            unz_file_info64 info = {};
            if (unzGetCurrentFileInfo64(zip, &info, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK || info.uncompressed_size > MaxThemeFileSize) {
                return false;
            }
            if (unzOpenCurrentFile(zip) != UNZ_OK) {
                return false;
            }
            bool ok = false;
            if (FILE *file = std::fopen(path.c_str(), "wb"); file != nullptr) {
                std::vector<char> buffer(64 * 1024);
                int read = 0;
                ok = true;
                while ((read = unzReadCurrentFile(zip, buffer.data(), static_cast<unsigned>(buffer.size()))) > 0) {
                    ok = ok && std::fwrite(buffer.data(), 1, static_cast<size_t>(read), file) == static_cast<size_t>(read);
                }
                ok = ok && read == 0;
                std::fclose(file);
            }
            unzCloseCurrentFile(zip);
            return ok;
        }

        void InstallZip(const std::string &zip_path, const std::string &folder) {
            unzFile zip = unzOpen64(zip_path.c_str());
            if (zip == nullptr) {
                return;
            }
            std::string prefix;
            if (FindThemeRoot(zip, &prefix)) {
                sd::MakeDirectories(folder);
                for (int rc = unzGoToFirstFile(zip); rc == UNZ_OK; rc = unzGoToNextFile(zip)) {
                    const std::string name = CurrentEntryName(zip);
                    if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0) {
                        continue;
                    }
                    const std::string relative = name.substr(prefix.size());
                    if (!IsSafeRelativePath(relative)) {
                        continue;
                    }
                    const std::string path = folder + "/" + relative;
                    if (relative.back() == '/') {
                        sd::MakeDirectories(path.substr(0, path.size() - 1));
                        continue;
                    }
                    sd::MakeDirectories(path.substr(0, path.rfind('/')));
                    ExtractCurrentFile(zip, path);
                }
            }
            unzClose(zip);
        }

    }

    void InstallZips() {
        std::vector<std::string> zips;
        if (DIR *directory = opendir(ThemesDirectory.c_str()); directory != nullptr) {
            while (const dirent *entry = readdir(directory)) {
                if (EndsWithZip(entry->d_name)) {
                    zips.push_back(entry->d_name);
                }
            }
            closedir(directory);
        }

        for (const auto &zip : zips) {
            const std::string zip_path = ThemePath(zip), folder = ThemePath(zip.substr(0, zip.size() - 4));
            /* Unpacked once, and again when the .zip is newer than what came out of it. Files are only added or replaced. */
            const s64 unpacked = ModifiedTime(folder + "/theme.json");
            if (unpacked >= 0 && unpacked >= ModifiedTime(zip_path)) {
                continue;
            }
            InstallZip(zip_path, folder);
        }
    }

    std::vector<std::string> List() {
        std::vector<std::string> names;
        if (DIR *directory = opendir(ThemesDirectory.c_str()); directory != nullptr) {
            while (const dirent *entry = readdir(directory)) {
                const std::string name = entry->d_name;
                if (name != "." && name != ".." && Exists(ThemePath(name) + "/theme.json")) {
                    names.push_back(name);
                }
            }
            closedir(directory);
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    namespace {

        std::string Value(const json::Table &table, const char *key) {
            const auto found = table.find(key);
            return found != table.end() ? found->second : std::string();
        }

        /* A number within [low, high]; `fallback` when the key is missing or is not a number. */
        double Number(const json::Table &table, const char *key, double low, double high, double fallback) {
            const std::string text = Value(table, key);
            char *end = nullptr;
            const double value = std::strtod(text.c_str(), &end);
            return text.empty() || end == text.c_str() ? fallback : std::clamp(value, low, high);
        }

        bool OnOff(const json::Table &table, const char *key, bool fallback) {
            const std::string text = Value(table, key);
            return text.empty() ? fallback : text != "off";
        }

        double HeaderBottom(const Layout &l) {
            return l.toolbar ? l.toolbar_y + l.toolbar_h : 0;
        }

        double FooterTop(const Layout &l) {
            return DesignHeight - (l.dock ? l.dock_h : 0);
        }

        /* The name, details and buttons under the carousel need about 160 px above the dock. */
        bool CarouselFits(const Layout &l) {
            return l.carousel_y >= HeaderBottom(l) && l.carousel_y + l.big_cover + 52 + 160 <= FooterTop(l) && l.small_cover <= l.big_cover;
        }

        bool GridFits(const Layout &l) {
            const double width = l.grid_columns * l.grid_cover + (l.grid_columns - 1) * l.grid_gap_x;
            const double height = l.grid_rows * (l.grid_cover + 52) + (l.grid_rows - 1) * l.grid_gap_y;
            return width <= DesignWidth - 2 * l.edge && l.grid_y >= HeaderBottom(l) && l.grid_y + height <= FooterTop(l) - 8;
        }

        Layout ReadLayout(const json::Table &table) {
            const Layout defaults;
            Layout l;
            l.edge = Number(table, "Layout.Edge", 24, 96, defaults.edge);
            l.toolbar = OnOff(table, "Layout.Toolbar.Visible", true);
            l.toolbar_y = Number(table, "Layout.Toolbar.Y", 0, 40, defaults.toolbar_y);
            l.toolbar_h = Number(table, "Layout.Toolbar.Height", 56, 120, defaults.toolbar_h);
            l.free_space = OnOff(table, "Layout.FreeSpace.Visible", true);
            l.dock = OnOff(table, "Layout.Dock.Visible", true);
            l.dock_h = Number(table, "Layout.Dock.Height", 44, 90, defaults.dock_h);
            l.window_w = Number(table, "Layout.Window.Width", 760, 1200, defaults.window_w);
            l.window_h = Number(table, "Layout.Window.Height", 440, 640, defaults.window_h);

            l.carousel_y = Number(table, "Carousel.Y", 0, 400, defaults.carousel_y);
            l.big_cover = Number(table, "Carousel.BigCover", 160, 360, defaults.big_cover);
            l.small_cover = Number(table, "Carousel.SmallCover", 80, 280, defaults.small_cover);
            l.carousel_gap = Number(table, "Carousel.Gap", 0, 80, defaults.carousel_gap);
            l.carousel_arc = Number(table, "Carousel.Arc", -120, 200, defaults.carousel_arc);
            l.carousel_scale = Number(table, "Carousel.Scale", 0.6, 1.0, defaults.carousel_scale);
            l.carousel_count = static_cast<s32>(Number(table, "Carousel.Count", 1, 8, defaults.carousel_count));
            if (!CarouselFits(l)) {
                l.carousel_y = defaults.carousel_y;
                l.big_cover = defaults.big_cover;
                l.small_cover = defaults.small_cover;
                if (!CarouselFits(l)) {
                    l.toolbar_y = defaults.toolbar_y;
                    l.toolbar_h = defaults.toolbar_h;
                    l.dock_h = defaults.dock_h;
                }
            }

            l.grid_columns = static_cast<s32>(Number(table, "Grid.Columns", 3, 8, defaults.grid_columns));
            l.grid_rows = static_cast<s32>(Number(table, "Grid.Rows", 1, 4, defaults.grid_rows));
            l.grid_y = Number(table, "Grid.Y", 0, 400, defaults.grid_y);
            l.grid_cover = Number(table, "Grid.Cover", 100, 300, defaults.grid_cover);
            l.grid_gap_x = Number(table, "Grid.GapX", 0, 80, defaults.grid_gap_x);
            l.grid_gap_y = Number(table, "Grid.GapY", 0, 80, defaults.grid_gap_y);
            /* Too many covers for their size: shrink them to fit before giving up on the theme's grid. */
            for (s32 tries = 0; !GridFits(l) && tries < 40 && l.grid_cover > 100; ++tries) {
                l.grid_cover -= 5;
            }
            if (!GridFits(l)) {
                l.grid_columns = defaults.grid_columns;
                l.grid_rows = defaults.grid_rows;
                l.grid_y = defaults.grid_y;
                l.grid_cover = defaults.grid_cover;
                l.grid_gap_x = defaults.grid_gap_x;
                l.grid_gap_y = defaults.grid_gap_y;
            }
            return l;
        }

        /* A file a theme names, inside its own folder only. */
        std::string ThemeFileOf(const std::string &theme, const std::string &relative) {
            if (theme.empty() || relative.empty() || relative.find("..") != std::string::npos) {
                return {};
            }
            const std::string path = ThemePath(theme) + "/" + relative;
            return Exists(path) ? path : std::string();
        }

        std::string ThemeFile(const std::string &relative) {
            return ThemeFileOf(g_current, relative);
        }

        pu::ui::Color DefaultColor(const char *name) {
            for (const auto &slot : g_slots) {
                if (std::string_view(slot.name) == name) {
                    return slot.default_value;
                }
            }
            return {};
        }

        Easing ParseEasing(const std::string &value) {
            constexpr std::pair<std::string_view, Easing> Names[] = {
                { "linear", Easing::Linear },
                { "out-quad", Easing::OutQuad },
                { "in-out-sine", Easing::InOutSine },
                { "out-back", Easing::OutBack },
                { "out-elastic", Easing::OutElastic },
            };
            for (const auto &[text, easing] : Names) {
                if (value == text) {
                    return easing;
                }
            }
            return Easing::OutCubic;
        }

        std::string Signature(const std::string &name) {
            if (name.empty()) {
                return {};
            }
            std::string signature;
            for (const std::string &folder : { ThemePath(name), ThemePath(name) + "/sfx" }) {
                DIR *dir = opendir(folder.c_str());
                if (dir == nullptr) {
                    continue;
                }
                while (const dirent *entry = readdir(dir)) {
                    struct stat info = {};
                    if (entry->d_name[0] == '.' || stat((folder + "/" + entry->d_name).c_str(), &info) != 0 || S_ISDIR(info.st_mode)) {
                        continue;
                    }
                    signature += std::string(entry->d_name) + ":" + std::to_string(info.st_size) + ":" + std::to_string(static_cast<long long>(info.st_mtime)) + ";";
                }
                closedir(dir);
            }
            return signature;
        }

        void ApplyTheme(const std::string &name) {
            for (auto &slot : g_slots) {
                *slot.color = slot.default_value;
            }

            json::Table table;
            g_current = !name.empty() && json::ReadFlatFile(&table, ThemePath(name) + "/theme.json") ? name : std::string();
            g_signature = Signature(g_current);
            for (auto &slot : g_slots) {
                if (const auto found = table.find(std::string("Colors.") + slot.name); found != table.end()) {
                    ParseColor(found->second, slot.color);
                }
            }

            g_display_name = Value(table, "Theme.Name").empty() ? g_current : Value(table, "Theme.Name");

            const std::string speed = Value(table, "Animations.Speed");
            MotionSpeed = speed.empty() ? 1.0 : std::clamp(std::strtod(speed.c_str(), nullptr), 0.25, 4.0);
            FloatAmount = Value(table, "Animations.Floating") == "off" ? 0.0 : 1.0;
            g_particles = Value(table, "Animations.Particles") != "off";
            const std::string pulse = Value(table, "Animations.FocusPulse");
            FocusPeriod = pulse == "off" ? 0.0 : (pulse.empty() ? 1.6 : std::clamp(std::strtod(pulse.c_str(), nullptr), 0.5, 6.0));
            TransitionEasing = ParseEasing(Value(table, "Animations.Easing"));
            layout = ReadLayout(table);
            g_components.clear();
            for (const auto &[key, value] : table) {
                for (const char *screen : ComponentScreens) {
                    const std::string prefix = std::string("Layout.") + screen + ".";
                    if (key.compare(0, prefix.size(), prefix) != 0) {
                        continue;
                    }
                    const std::string rest = key.substr(prefix.size());
                    const size_t dot = rest.rfind('.');
                    if (dot != std::string::npos && dot > 0 && dot + 1 < rest.size()) {
                        g_components[screen][rest.substr(0, dot)][rest.substr(dot + 1)] = value;
                    }
                }
            }

            g_background = nullptr;
            g_animated_background = nullptr;
            if (const std::string image = ThemeFile(Value(table, "Images.Background")); !image.empty()) {
                g_animated_background = AnimatedImage::Open(image);
                SDL_Texture *texture = g_animated_background == nullptr ? pu::ui::render::LoadImageFromFile(image) : nullptr;
                if (texture != nullptr) {
                    g_background = pu::sdl2::TextureHandle::New(texture);
                }
            }
            g_background_tiled = Value(table, "Images.BackgroundMode") == "tile";
            const std::string dim = Value(table, "Images.BackgroundDim");
            g_background_dim = dim.empty() ? 0.4 : std::clamp(std::strtod(dim.c_str(), nullptr), 0.0, 0.9);
            g_music = ThemeFile(Value(table, "Sounds.Music"));

            for (size_t i = 0; i < std::size(SkinNames); ++i) {
                const std::string key = std::string("Images.") + SkinNames[i];
                g_skins[i] = {};
                if (const std::string file = ThemeFile(Value(table, key.c_str())); !file.empty()) {
                    if (SDL_Texture *texture = pu::ui::render::LoadImageFromFile(file); texture != nullptr) {
                        g_skins[i].handle = pu::sdl2::TextureHandle::New(texture);
                        g_skins[i].image = { texture, Number(table, (key + "Border").c_str(), 0, 256, 16) };
                    }
                }
            }
            for (size_t i = 0; i < std::size(IconNames); ++i) {
                g_icons[i] = nullptr;
                if (const std::string file = ThemeFile(Value(table, (std::string("Images.Icon.") + IconNames[i]).c_str())); !file.empty()) {
                    if (SDL_Texture *texture = pu::ui::render::LoadImageFromFile(file); texture != nullptr) {
                        g_icons[i] = pu::sdl2::TextureHandle::New(texture);
                    }
                }
            }
            g_tint_icons = Value(table, "Images.IconTint") == "on";
            g_views.clear();
            const std::string views = Value(table, "Views.Enabled");
            for (size_t start = 0; start < views.size();) {
                size_t end = views.find(',', start);
                if (end == std::string::npos) {
                    end = views.size();
                }
                std::string view = views.substr(start, end - start);
                view.erase(0, view.find_first_not_of(' '));
                view.erase(view.find_last_not_of(' ') + 1);
                if (!view.empty()) {
                    g_views.push_back(view);
                }
                start = end + 1;
            }
            g_default_view = Value(table, "Views.Default");
            const std::string backdrop = Value(table, "Images.GameBackdrop");
            g_game_backdrop = backdrop.empty() ? Value(table, "Images.Background").empty() : backdrop != "off";

            const std::string font_scale = Value(table, "Fonts.Scale");
            const double scale = font_scale.empty() ? 1.0 : std::clamp(std::strtod(font_scale.c_str(), nullptr), 0.8, 1.25);
            const std::string font_file = ThemeFile(Value(table, "Fonts.Regular"));
            g_font = font_file.empty() ? nullptr : LoadFont(font_file, scale);
        }

    }

    void Apply(const std::string &name) {
        ApplyTheme(name);
        settings::SetString(ThemeKey, g_current);
    }

    void ApplySaved() {
        ApplyTheme(settings::GetString(ThemeKey, {}));
    }

    const SkinImage *GetSkin(Skin skin) {
        const auto &loaded = g_skins[static_cast<size_t>(skin)];
        return loaded.handle != nullptr ? &loaded.image : nullptr;
    }

    SDL_Texture *GetIcon(Icon icon) {
        const auto &handle = g_icons[static_cast<size_t>(icon)];
        return handle != nullptr ? handle->Get() : nullptr;
    }

    const ComponentProperties *FindComponent(const std::string &screen, const std::string &id) {
        const auto found_screen = g_components.find(screen);
        if (found_screen == g_components.end()) {
            return nullptr;
        }
        const auto found = found_screen->second.find(id);
        return found != found_screen->second.end() ? &found->second : nullptr;
    }

    const std::map<std::string, ComponentProperties> &ScreenComponents(const std::string &screen) {
        static const std::map<std::string, ComponentProperties> none;
        const auto found = g_components.find(screen);
        return found != g_components.end() ? found->second : none;
    }

    std::string ThemeAsset(const std::string &relative) {
        return ThemeFile(relative);
    }

    bool ShelvesByGenre() {
        return settings::GetInt("shelves_by_genre", 0) != 0;
    }

    bool SetShelvesByGenre(bool by_genre) {
        return settings::SetInt("shelves_by_genre", by_genre ? 1 : 0);
    }

    bool IsGameBackdropEnabled() {
        return settings::GetInt(GameBackdropKey, 1) != 0;
    }

    bool SetGameBackdropEnabled(bool enabled) {
        return settings::SetInt(GameBackdropKey, enabled ? 1 : 0);
    }

    bool ShowsGameBackdrop() {
        return g_game_backdrop && IsGameBackdropEnabled();
    }

    std::vector<std::string> EnabledViews() {
        return g_views;
    }

    std::string DefaultView() {
        return g_default_view;
    }

    bool TintsIcons() {
        return g_tint_icons;
    }

    std::string FontName(u32 design_size) {
        if (g_font == nullptr) {
            /* A size the renderer was not given at start (a theme's FontSize): the console's fonts at that size. */
            static std::vector<u32> registered(std::begin(FontSizes), std::end(FontSizes));
            const u32 px = static_cast<u32>(S(design_size));
            if (std::find(registered.begin(), registered.end(), design_size) == registered.end()) {
                auto font = std::make_shared<pu::ttf::Font>(px);
                pu::ui::render::LoadAllSharedFontsInFont(font);
                pu::ui::render::AddFont(pu::ui::MakeDefaultFontName(px), font);
                registered.push_back(design_size);
            }
            return pu::ui::MakeDefaultFontName(px);
        }
        const u32 px = static_cast<u32>(S(design_size * g_font->scale));
        if (std::find(g_font->registered.begin(), g_font->registered.end(), px) == g_font->registered.end()) {
            /* The theme's font first; the console's fonts after it for what it lacks (other scripts, button glyphs). */
            auto font = std::make_shared<pu::ttf::Font>(px);
            font->LoadFromMemory(g_font->data.get(), g_font->size, pu::ttf::Font::EmptyFontFaceDisposingFunction);
            pu::ui::render::LoadAllSharedFontsInFont(font);
            pu::ui::render::AddFont(ThemeFontName(*g_font, px), font);
            g_font->registered.push_back(px);
        }
        return ThemeFontName(*g_font, px);
    }

    bool ReloadIfChanged() {
        if (g_current.empty() || Signature(g_current) == g_signature) {
            return false;
        }
        ApplyTheme(g_current);
        return true;
    }

    void Release() {
        g_animated_background = nullptr;
        g_background = nullptr;
        for (auto &skin : g_skins) {
            skin = {};
        }
        for (auto &icon : g_icons) {
            icon = nullptr;
        }
    }

    const std::string &Current() {
        return g_current;
    }

    std::string FindAsset(const std::string &relative) {
        return ThemeFile(relative);
    }

    std::string DisplayName() {
        return g_display_name;
    }

    SDL_Texture *BackgroundImage() {
        if (g_animated_background != nullptr) {
            return g_animated_background->Frame();
        }
        return g_background != nullptr ? g_background->Get() : nullptr;
    }

    double BackgroundDim() {
        return g_background_dim;
    }

    bool IsBackgroundTiled() {
        return g_background_tiled;
    }

    bool HasParticles() {
        return g_particles;
    }

    std::string MusicPath() {
        return g_music;
    }

    std::vector<Preview> ListPreviews() {
        const auto colors = [](const json::Table &table, Preview *out) {
            const auto pick = [&table](const char *name) {
                pu::ui::Color color = DefaultColor(name);
                if (const auto found = table.find(std::string("Colors.") + name); found != table.end()) {
                    ParseColor(found->second, &color);
                }
                return color;
            };
            out->ground = pick("Ground");
            out->panel = pick("Panel");
            out->raised = pick("Raised");
            out->primary = pick("Primary");
            out->text = pick("Text");
        };

        std::vector<Preview> previews(1);
        colors({}, &previews[0]);
        for (const auto &name : List()) {
            json::Table table;
            if (!json::ReadFlatFile(&table, ThemePath(name) + "/theme.json")) {
                continue;
            }
            Preview preview = { .name = name, .display_name = Value(table, "Theme.Name").empty() ? name : Value(table, "Theme.Name") };
            /* "Format": { "Version": "N" }; a newer theme still loads what this version understands. */
            if (std::strtol(Value(table, "Format.Version").c_str(), nullptr, 10) > usbl::formats::Theme) {
                preview.display_name += " (" + strings::Get(strings::Id::ThemeFromNewerVersion) + ")";
            }
            colors(table, &preview);
            preview.author = Value(table, "Theme.Author");
            preview.version = Value(table, "Theme.Version");
            if (const std::string background = ThemeFileOf(name, Value(table, "Images.Background")); !background.empty()) {
                preview.animated = AnimatedImage::IsAnimated(background);
                preview.background = !preview.animated;
            }
            preview.music = !ThemeFileOf(name, Value(table, "Sounds.Music")).empty();
            preview.font = !ThemeFileOf(name, Value(table, "Fonts.Regular")).empty();
            if (DIR *sfx = opendir((ThemePath(name) + "/sfx").c_str()); sfx != nullptr) {
                while (const dirent *entry = readdir(sfx)) {
                    if (entry->d_name[0] != '.') {
                        preview.sounds = true;
                        break;
                    }
                }
                closedir(sfx);
            }
            for (const auto &[key, value] : table) {
                const auto starts = [&key](std::string_view prefix) { return key.compare(0, prefix.size(), prefix) == 0; };
                preview.layout = preview.layout || starts("Layout.") || starts("Carousel.") || starts("Grid.");
                preview.images = preview.images || (starts("Images.") && !starts("Images.Background") && key != "Images.Thumbnail");
            }
            for (const std::string &candidate : { Value(table, "Images.Thumbnail"), std::string("thumb.png"), std::string("thumb.jpg"), Value(table, "Images.Background") }) {
                if (preview.thumbnail = ThemeFileOf(name, candidate); !preview.thumbnail.empty()) {
                    break;
                }
            }
            previews.push_back(std::move(preview));
        }
        return previews;
    }

}
