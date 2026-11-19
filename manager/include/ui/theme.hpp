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

    /* Manager themes: sdmc:/config/usbloadernx-manager/themes/<name>/ with a theme.json (colors, animations, background
     * image, music; see manager/themes/README.md) and optionally sfx/<effect>.wav in place of the built-in sounds.
     * Anything a theme leaves out keeps the default. */
    namespace theme {

        /* The installed themes' folder names, sorted; the built-in theme is "". */
        std::vector<std::string> List();
        /* Applies a theme (or the built-in one for "" or a theme that cannot be read) and remembers it in manager.ini. */
        void Apply(const std::string &name);
        /* The theme saved in manager.ini, applied at start. */
        void ApplySaved();
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

        /* What the theme picker shows of a theme without applying it. */
        struct Preview {
            /* The folder name; "" for the built-in theme. */
            std::string name;
            /* "Theme.Name", else the folder name; "" for the built-in theme. */
            std::string display_name;
            /* "Images.Thumbnail", else thumb.png or thumb.jpg, else the background image; "" when it has none. */
            std::string thumbnail;
            pu::ui::Color ground, panel, raised, primary, text;
        };

        /* The built-in theme first, then the installed ones, sorted. */
        std::vector<Preview> ListPreviews();

    }

    /* Game icons get rounded corners of this fraction of their side, like on HOME; frames around them follow it. */
    constexpr double IconCornerRatio = 0.11;

    /* Text sizes used by the screens, in design px; main registers each one with the renderer. */
    constexpr u32 FontSizes[] = { 13, 14, 15, 16, 17, 18, 19, 20, 21, 26, 28, 30, 40, 46 };

    inline std::string Font(u32 design_size) {
        return pu::ui::MakeDefaultFontName(S(design_size));
    }

}
