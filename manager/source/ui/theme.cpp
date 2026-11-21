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
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <string_view>
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
        bool g_background_tiled = false;
        double g_background_dim = 0.4;
        bool g_particles = true;
        std::string g_music;

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

        void ApplyTheme(const std::string &name) {
            for (auto &slot : g_slots) {
                *slot.color = slot.default_value;
            }

            json::Table table;
            g_current = !name.empty() && json::ReadFlatFile(&table, ThemePath(name) + "/theme.json") ? name : std::string();
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

            g_background = nullptr;
            if (const std::string image = ThemeFile(Value(table, "Images.Background")); !image.empty()) {
                if (SDL_Texture *texture = pu::ui::render::LoadImageFromFile(image); texture != nullptr) {
                    g_background = pu::sdl2::TextureHandle::New(texture);
                }
            }
            g_background_tiled = Value(table, "Images.BackgroundMode") == "tile";
            const std::string dim = Value(table, "Images.BackgroundDim");
            g_background_dim = dim.empty() ? 0.4 : std::clamp(std::strtod(dim.c_str(), nullptr), 0.0, 0.9);
            g_music = ThemeFile(Value(table, "Sounds.Music"));
        }

    }

    void Apply(const std::string &name) {
        ApplyTheme(name);
        settings::SetString(ThemeKey, g_current);
    }

    void ApplySaved() {
        ApplyTheme(settings::GetString(ThemeKey, {}));
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
