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
#include <string>
#include <ui/theme.hpp>

/* Shapes and icons of the design, antialiased (SDL_RenderGeometry with a feathered rim). All coordinates and sizes are
 * design px (1280x720). */
namespace ui::draw {

    using pu::ui::Color;

    /* Moves everything drawn while it lives (a window sliding in); nests. */
    class Translate {
        private:
            double previous_x, previous_y;

        public:
            Translate(double dx, double dy);
            ~Translate();
            Translate(const Translate &) = delete;
            Translate &operator=(const Translate &) = delete;
    };

    void Rect(double x, double y, double w, double h, Color c);
    void RoundRect(double x, double y, double w, double h, double radius, Color c);
    /* A rounded border of `thickness` around a filled inside. */
    void FrameRect(double x, double y, double w, double h, double radius, double thickness, Color border, Color fill);
    /* Only the border, `thickness` wide, inside the rectangle. */
    void RoundOutline(double x, double y, double w, double h, double radius, double thickness, Color c);
    void Circle(double cx, double cy, double r, Color c);
    void Ring(double cx, double cy, double r, double thickness, Color c, Color fill);
    void Line(double x1, double y1, double x2, double y2, double width, Color c);
    void Triangle(double x1, double y1, double x2, double y2, double x3, double y3, Color c);

    /* A texture stretched to the rectangle, with `alpha` opacity. */
    void Image(SDL_Texture *texture, double x, double y, double w, double h, u8 alpha = 0xFF);

    /* Part of a texture (`source`, in texture px) stretched to the rectangle. */
    void ImagePart(SDL_Texture *texture, const SDL_Rect &source, double x, double y, double w, double h, u8 alpha = 0xFF);
    /* A texture drawn `w` x `h` centered on (cx, cy), turned `degrees` clockwise around its center. */
    void ImageTurned(SDL_Texture *texture, double cx, double cy, double w, double h, double degrees, u8 alpha = 0xFF);

    /* A texture mapped onto any four corners (top left, top right, bottom right, bottom left), each with its own
     * opacity; `flip` turns the picture upside down (a reflection), showing only its bottom `span` (0..1). */
    struct Corner {
        double x, y;
        u8 alpha;
    };
    void ImageQuad(SDL_Texture *texture, const Corner (&corners)[4], bool flip = false, double span = 1.0);

    /* `source` center-cropped to 16:9 and shrunk to a few pixels; drawn back over the screen with linear filtering, it
     * is a soft blur of the picture. Null when the renderer cannot draw into a texture. */
    SDL_Texture *MakeBlurred(SDL_Texture *source);

    /* A texture stretched to the rectangle as nine slices: its corners, `border` texture px wide, keep their size. */
    void NineSlice(SDL_Texture *texture, double border, double x, double y, double w, double h, u8 alpha = 0xFF);
    /* The theme's image for `skin` over the rectangle; false when the theme has none (draw the built-in shape then). */
    bool Skinned(theme::Skin skin, double x, double y, double w, double h, u8 alpha = 0xFF);

    /* The screen behind everything: the theme's image when it has one, and the drifting specks, unless the theme
     * turns them off. */
    void Background();
    /* Soft specks of the palette drifting up the screen, at `seconds` into their loop. */
    void Ambient(double seconds);

    /* `from` towards `to` by `t` (0..1), alpha included. */
    Color Mix(Color from, Color to, double t);

    /* The focus ring around a rounded rectangle: `gap` design px out from it, 4 px wide, pulsing between Focus and
     * FocusAlt with a glow that swells with the pulse. */
    void FocusRing(double x, double y, double w, double h, double radius, double gap = 5);

    /* How full a drive is, as N-D-B's status colors: Primary up to 60%, then Warning, Pending and Error at 100%. */
    Color UsageColor(double usage);
    /* A rounded bar `usage` (0..1) full, its fill running through UsageColor from empty to `usage`. */
    void UsageBar(double x, double y, double w, double h, double usage);

    /* The circle (one letter) or pill behind a controller button label ("A", "ZL", "+"); the label is a TextBlock on top. */
    void ButtonGlyph(double cx, double cy, double width);

    /* Icons, centered on (cx, cy), `size` design px wide, drawn as strokes like the design's. */
    void IconUsbStick(double cx, double cy, double size, Color c);
    void IconClock(double cx, double cy, double size, Color c);
    void IconWarning(double cx, double cy, double size, Color c);
    void Chevron(double cx, double cy, double size, Color c);
    /* A chevron pointing left: more of a row before. */
    void ChevronLeft(double cx, double cy, double size, Color c);
    /* A chevron pointing up or down: more of a list above or below. */
    void ChevronVertical(double cx, double cy, double size, bool down, Color c);
    /* A five-pointed star (favorites), `r` from its center to a point. */
    void Star(double cx, double cy, double r, Color c);
    /* A parcel: an eShop download (NSP). */
    void IconPackage(double cx, double cy, double size, Color c);
    void IconGrid(double cx, double cy, double size, Color c);
    /* A Switch game card outline, `w` wide. */
    void GameCard(double x, double y, double w, double h, Color body, Color grip);

    /* An element that runs a draw function in layout order, so shapes interleave with images and text. */
    class Painter : public pu::ui::elm::Element {
        private:
            std::function<void()> paint;

        public:
            explicit Painter(std::function<void()> paint) : Element(), paint(std::move(paint)) {}
            PU_SMART_CTOR(Painter)

            s32 GetX() override { return 0; }
            s32 GetY() override { return 0; }
            s32 GetWidth() override { return pu::ui::render::ScreenWidth; }
            s32 GetHeight() override { return pu::ui::render::ScreenHeight; }

            void OnRender(pu::ui::render::Renderer::Ref &, const s32, const s32) override { this->paint(); }
            void OnInput(const u64, const u64, const u64, const pu::ui::TouchPoint) override {}
    };

}
