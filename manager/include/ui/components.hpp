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
#include <string>
#include <vector>
#include <ui/widgets.hpp>

/* A screen as named components a theme can move, resize, restyle or hide (Layout.<Screen>.<id>.*), and components a
 * theme adds of its own (a clock, the battery, text, pictures). Positions are design px on the 1280x720 screen. */
namespace ui::components {

    struct Placement {
        bool visible = true;
        double x = 0, y = 0, w = 0, h = 0;
        /* Text size in design px; 0 for components without text. */
        u32 font_size = 0;
        Align align = Align::Left;
    };

    /* Where the component goes: `defaults` as the Manager lays it out, with what the theme says of it on top:
     *   Anchor     top-left (default), top, top-right, left, center, right, bottom-left, bottom, bottom-right: the
     *              screen's point the component's own same point sits at, moved by X and Y
     *   X, Y       that offset (without Anchor, from the screen's top left)
     *   Width, Height, FontSize (8 to 72), Align (left, center, right), Visible (off hides it)
     * The result always lies within the screen. */
    Placement Place(const char *screen, const char *id, const Placement &defaults);

    /* Where AddText takes its x for this placement's alignment (the left edge, the middle or the right edge). */
    double TextX(const Placement &place);

    /* A component the theme adds ("Type" set): drawn by the screen with Draw. */
    enum class Kind {
        Clock,
        Battery,
        Text,
        Image,
        /* The selected game's cover, case or icon. */
        Cover,
        Box,
        Icon,
    };

    struct Free {
        std::string id;
        Kind kind;
        Placement place;
        /* Text: "{{game.name}}"-style fields filled by the screen; Image: the theme's file. */
        std::string text, image;
        pu::ui::Color color;
        bool has_color = false;
    };

    std::vector<Free> FreeComponents(const char *screen);

}
