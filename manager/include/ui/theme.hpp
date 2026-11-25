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
#include <pu/Plutonium>
#include <map>
#include <string>
#include <vector>

/* The approved design is drawn at 1280x720; Plutonium renders at 1920x1080. Every coordinate goes through S(). */
namespace ui {

    constexpr double Scale = static_cast<double>(pu::ui::render::ScreenWidth) / 1280.0;

    constexpr s32 S(double design_px) {
        return static_cast<s32>(design_px * Scale + (design_px >= 0 ? 0.5 : -0.5));
    }

    constexpr s32 DesignWidth  = 1280;
    constexpr s32 DesignHeight = 720;

    /* The palette: N-D-B's by default (the badge and notification colors, plus darker grounds derived from its default).
     * A theme replaces any of them (theme::Apply); everything reads them at draw time. */
    namespace color {
        using pu::ui::Color;
        inline Color Ground    = { 0x1E, 0x1F, 0x22, 0xFF };
        inline Color Panel     = { 0x2B, 0x2D, 0x31, 0xFF };
        inline Color Raised    = { 0x35, 0x37, 0x3C, 0xFF };
        inline Color Track     = { 0x40, 0x42, 0x49, 0xFF };
        inline Color Text      = { 0xF2, 0xF3, 0xF5, 0xFF };
        inline Color TextSoft  = { 0xDB, 0xDE, 0xE1, 0xFF };
        inline Color Muted     = { 0xA3, 0xA6, 0xAD, 0xFF };
        inline Color MutedSoft = { 0xB5, 0xBA, 0xC1, 0xFF };
        inline Color Faint     = { 0x80, 0x84, 0x8E, 0xFF };
        inline Color Primary   = { 0x00, 0xC2, 0x6F, 0xFF };
        inline Color Info      = { 0x00, 0xB8, 0xC2, 0xFF };
        inline Color Warning   = { 0xC2, 0xB2, 0x00, 0xFF };
        inline Color Pending   = { 0xC2, 0x78, 0x00, 0xFF };
        inline Color Error     = { 0xC2, 0x0E, 0x00, 0xFF };
        inline Color ErrorText = { 0xFF, 0x6B, 0x5E, 0xFF };
        /* Text and glyphs on a Primary button. */
        inline Color OnPrimary = { 0x0E, 0x1A, 0x13, 0xFF };
        /* The focus ring pulses between these two. */
        inline Color Focus     = { 0x00, 0xC2, 0x6F, 0xFF };
        inline Color FocusAlt  = { 0x00, 0xB8, 0xC2, 0xFF };
        inline Color Star      = { 0xE3, 0xC8, 0x00, 0xFF };
        /* A line or box that needs attention (an update on another drive, fragmentation). */
        inline Color Alert        = { 0xE8, 0x9A, 0x2E, 0xFF };
        inline Color AlertSurface = { 0x3A, 0x30, 0x20, 0xFF };
        inline Color AlertText    = { 0xF3, 0xD3, 0xA0, 0xFF };
        /* Over the screen behind a window. */
        inline Color Overlay   = { 0x0C, 0x0C, 0x0E, 0xD1 };

        constexpr Color WithAlpha(Color c, u8 a) {
            return { c.r, c.g, c.b, a };
        }
    }

    /* Where the home screen puts things, in design px; the defaults are the approved design. A theme replaces any of
     * them (theme::Apply, which keeps values that would not fit the screen at their defaults). */
    struct Layout {
        double edge = 48;
        bool toolbar = true;
        double toolbar_y = 0, toolbar_h = 80;
        bool free_space = true;
        bool dock = true;
        double dock_h = 60;
        /* The carousel: the selected cover large at the left, `count` more queued after it. Each queued cover is
         * `scale` times the one before it, and the last one sits `arc` px lower than the first, along a curve. */
        double carousel_y = 104, big_cover = 280, small_cover = 184, carousel_gap = 24, carousel_arc = 0, carousel_scale = 1;
        s32 carousel_count = 5;
        s32 grid_columns = 5, grid_rows = 2;
        double grid_y = 104, grid_cover = 200, grid_gap_x = 32, grid_gap_y = 24;
        /* The game window. */
        double window_w = 960, window_h = 516;
    };
    inline Layout layout;

    /* Manager themes: sdmc:/config/usbloadernx-manager/themes/<name>/ with a theme.json (colors, animations, background
     * image, music; see manager/themes/README.md) and optionally sfx/<effect>.wav in place of the built-in sounds.
     * Anything a theme leaves out keeps the default. */
    namespace theme {

        /* Unpacks each themes/<name>.zip into themes/<name>/ (again when the .zip is newer). Writes to the SD card. */
        void InstallZips();

        /* The installed themes' folder names, sorted; the built-in theme is "". */
        std::vector<std::string> List();
        /* Applies a theme (or the built-in one for "" or a theme that cannot be read) and remembers it in manager.ini. */
        void Apply(const std::string &name);
        /* The theme saved in manager.ini, applied at start. */
        void ApplySaved();
        /* Applies the theme again when any of its files changed since it was applied (a theme edited over FTP or
         * MTP, or sent by an editor); true when it did. Reads only the folders' listings. */
        bool ReloadIfChanged();

        /* Stops the animated background and frees the theme's textures while the renderer is still there. */
        void Release();
        const std::string &Current();
        /* The theme's "Theme.Name", else its folder name. */
        std::string DisplayName();
        /* The theme's own file for "Images.Background" / "Sounds.Music"; null or "" when it has none. */
        SDL_Texture *BackgroundImage();
        bool IsBackgroundTiled();
        /* How much of the ground color lies over the background image (0 to 0.9) so text stays readable. */
        double BackgroundDim();
        bool HasParticles();
        std::string MusicPath();
        /* The theme's own file for `relative` (e.g. "sfx/move.wav") when it has one, else "". */
        std::string FindAsset(const std::string &relative);

        /* The renderer's font for `design_size`: the theme's "Fonts.Regular" (scaled by "Fonts.Scale") over the
         * console's fonts, else the console's. */
        std::string FontName(u32 design_size);

        /* Theme images drawn in place of the built-in shapes (Images.<name>, stretched as nine slices: the corners
         * `border` texture px wide stay as they are). */
        enum class Skin {
            Panel,
            Card,
            CardFocused,
            Chip,
            Button,
            ButtonActive,
            FocusRing,
            Count,
        };
        struct SkinImage {
            SDL_Texture *texture;
            double border;
        };
        /* Null when the theme has no image for it. */
        const SkinImage *GetSkin(Skin skin);

        /* Theme icons (Images.Icon.<name>) drawn in place of the built-in strokes. */
        enum class Icon {
            Usb,
            Clock,
            Warning,
            Chevron,
            Star,
            Package,
            Grid,
            Count,
        };
        SDL_Texture *GetIcon(Icon icon);
        /* Images.IconTint: icons take the color they are drawn in (their own colors otherwise). */
        bool TintsIcons();

        /* The selected game's art, blurred, behind the home screen: the player's choice (manager.ini game_backdrop, on
         * by default) and the theme's (Images.GameBackdrop; a theme with its own background turns it on itself). */
        bool IsGameBackdropEnabled();
        bool SetGameBackdropEnabled(bool enabled);
        bool ShowsGameBackdrop();

        /* How the Shelves view groups games: by folder under usbloadernx/games (false) or by GameTDB genre (true);
         * manager.ini shelves_by_genre. */
        bool ShelvesByGenre();
        bool SetShelvesByGenre(bool by_genre);

        /* Layout.<Screen>.<component>.<Property> as written in theme.json ("Layout.Home.info.name.X" -> screen "Home",
         * component "info.name", property "X"); null when the theme says nothing of that component. ui/components
         * reads them. */
        using ComponentProperties = std::map<std::string, std::string>;
        const ComponentProperties *FindComponent(const std::string &screen, const std::string &id);
        /* Every component the theme names on a screen, by id. */
        const std::map<std::string, ComponentProperties> &ScreenComponents(const std::string &screen);
        /* A file in the theme's folder, "" when it has none (or it would leave the folder). */
        std::string ThemeAsset(const std::string &relative);

        /* Views.Enabled: the home screen views the theme offers ("carousel,grid,covers,boxes,shelves"); empty for
         * all of them. Views.Default: the one it starts in ("" when it does not say). */
        std::vector<std::string> EnabledViews();
        std::string DefaultView();

        /* What the theme picker shows of a theme without applying it. */
        struct Preview {
            /* The folder name; "" for the built-in theme. */
            std::string name;
            /* "Theme.Name", else the folder name; "" for the built-in theme. */
            std::string display_name;
            /* "Images.Thumbnail", else thumb.png or thumb.jpg, else the background image; "" when it has none. */
            std::string thumbnail;
            /* "Theme.Author" and "Theme.Version"; empty when the theme does not say. */
            std::string author, version;
            pu::ui::Color ground, panel, raised, primary, text;
            /* What the theme changes besides colors. */
            bool animated = false, background = false, music = false, font = false, sounds = false, layout = false, images = false;
        };

        /* The built-in theme first, then the installed ones, sorted. */
        std::vector<Preview> ListPreviews();

    }

    /* Game icons get rounded corners of this fraction of their side, like on HOME; frames around them follow it. */
    constexpr double IconCornerRatio = 0.11;

    /* Text sizes used by the screens, in design px; main registers each one with the renderer. */
    constexpr u32 FontSizes[] = { 13, 14, 15, 16, 17, 18, 19, 20, 21, 26, 28, 30, 40, 46 };

    inline std::string Font(u32 design_size) {
        return theme::FontName(design_size);
    }

}
