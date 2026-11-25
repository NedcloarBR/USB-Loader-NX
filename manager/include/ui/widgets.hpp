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
#include <vector>
#include <ui/draw.hpp>

/* Builders for a layout's elements, in design px. Elements render in the order they are added. */
namespace ui {

    enum class Align {
        Left,
        Center,
        Right,
    };

    /* `text` cut with an ellipsis to `max_width` design px (0 = no limit). */
    std::string Fit(const std::string &text, u32 size, double max_width);

    /* Word-wrapped to `max_width` design px in at most `max_lines` lines; only the last one may end in an ellipsis. */
    std::vector<std::string> WrapLines(const std::string &text, u32 size, double max_width, size_t max_lines);

    /* `x` is the left edge, the center or the right edge depending on `align`; `max_width` (0 = none) cuts longer text with an ellipsis. */
    pu::ui::elm::TextBlock::Ref AddText(pu::ui::Layout &layout, double x, double y, const std::string &text, u32 size, pu::ui::Color color,
                                        Align align = Align::Left, double max_width = 0);

    void AddPaint(pu::ui::Layout &layout, std::function<void()> paint);

    pu::ui::elm::Image::Ref AddImage(pu::ui::Layout &layout, pu::sdl2::TextureHandle::Ref texture, double x, double y, double size);

    /* A controller button label ("A", "ZL", "+") on a light circle or pill, centered on (cx, cy). */
    pu::ui::elm::TextBlock::Ref AddButtonHint(pu::ui::Layout &layout, double cx, double cy, const std::string &label);

    /* The touch screen, frame by frame: a press that ends where it started is a tap, one that moved is a swipe. */
    class TouchGesture {
        public:
            struct Event {
                enum class Kind {
                    None,
                    Tap,
                    Swipe,
                } kind = Kind::None;
                /* Design px: where a tap was, or how far a swipe went. */
                double x = 0, y = 0;
            };

            /* Call every frame with the point Plutonium gives (empty when nothing touches the screen). */
            Event Update(const pu::ui::TouchPoint &touch);

        private:
            bool down = false;
            double start_x = 0, start_y = 0, x = 0, y = 0;
    };

    /* A rectangle that does something when tapped. */
    struct TapTarget {
        double x, y, w, h;
        std::function<void()> on_tap;

        bool Contains(double px, double py) const {
            return px >= this->x && px < this->x + this->w && py >= this->y && py < this->y + this->h;
        }
    };

    /* Runs the last target (the topmost one) under the point; false when none is. */
    bool Tap(const std::vector<TapTarget> &targets, double x, double y);

    struct Hint {
        std::string button;
        std::string text;
    };

    /* A row of button hints ending at `right`, the first one rightmost, centered on `cy`; returns where the row starts. */
    double AddHintBar(pu::ui::Layout &layout, double right, double cy, const std::vector<Hint> &hints, pu::ui::Color text_color);

}
