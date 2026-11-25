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
#include <ui/components.hpp>
#include <algorithm>
#include <cstdlib>
#include <string_view>

namespace ui::components {

    namespace {

        const std::string *Property(const theme::ComponentProperties &properties, const char *name) {
            const auto found = properties.find(name);
            return found != properties.end() && !found->second.empty() ? &found->second : nullptr;
        }

        bool Number(const theme::ComponentProperties &properties, const char *name, double low, double high, double *out) {
            const std::string *text = Property(properties, name);
            if (text == nullptr) {
                return false;
            }
            char *end = nullptr;
            const double value = std::strtod(text->c_str(), &end);
            if (end == text->c_str()) {
                return false;
            }
            *out = std::clamp(value, low, high);
            return true;
        }

        /* Column and row of an anchor: 0, 1 or 2 (left/top, middle, right/bottom). */
        bool ParseAnchor(const std::string &anchor, int *column, int *row) {
            constexpr std::pair<std::string_view, std::pair<int, int>> Anchors[] = {
                { "top-left", { 0, 0 } }, { "top", { 1, 0 } }, { "top-right", { 2, 0 } },
                { "left", { 0, 1 } }, { "center", { 1, 1 } }, { "right", { 2, 1 } },
                { "bottom-left", { 0, 2 } }, { "bottom", { 1, 2 } }, { "bottom-right", { 2, 2 } },
            };
            for (const auto &[name, place] : Anchors) {
                if (anchor == name) {
                    *column = place.first;
                    *row = place.second;
                    return true;
                }
            }
            return false;
        }

        int HexDigit(char c) {
            return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
        }

        bool ParseColor(const std::string &text, pu::ui::Color *out) {
            if ((text.size() != 7 && text.size() != 9) || text[0] != '#') {
                return false;
            }
            u8 parts[4] = { 0, 0, 0, 0xFF };
            for (size_t i = 0; i < (text.size() - 1) / 2; ++i) {
                const int high = HexDigit(text[1 + i * 2]), low = HexDigit(text[2 + i * 2]);
                if (high < 0 || low < 0) {
                    return false;
                }
                parts[i] = static_cast<u8>(high * 16 + low);
            }
            *out = { parts[0], parts[1], parts[2], parts[3] };
            return true;
        }

        Placement Apply(const theme::ComponentProperties &properties, Placement place) {
            if (const std::string *visible = Property(properties, "Visible"); visible != nullptr) {
                place.visible = *visible != "off";
            }
            Number(properties, "Width", 1, DesignWidth, &place.w);
            Number(properties, "Height", 1, DesignHeight, &place.h);
            if (double size = 0; Number(properties, "FontSize", 8, 72, &size)) {
                place.font_size = static_cast<u32>(size);
            }
            if (const std::string *align = Property(properties, "Align"); align != nullptr) {
                place.align = *align == "center" ? Align::Center : (*align == "right" ? Align::Right : Align::Left);
            }

            int column = 0, row = 0;
            const std::string *anchor = Property(properties, "Anchor");
            const bool anchored = anchor != nullptr && ParseAnchor(*anchor, &column, &row);
            double x = 0, y = 0;
            const bool has_x = Number(properties, "X", -DesignWidth, DesignWidth, &x);
            const bool has_y = Number(properties, "Y", -DesignHeight, DesignHeight, &y);
            if (anchored || has_x) {
                place.x = column * DesignWidth / 2.0 + x - column * place.w / 2.0;
            }
            if (anchored || has_y) {
                place.y = row * DesignHeight / 2.0 + y - row * place.h / 2.0;
            }
            place.x = std::clamp(place.x, 0.0, std::max(0.0, DesignWidth - place.w));
            place.y = std::clamp(place.y, 0.0, std::max(0.0, DesignHeight - place.h));
            return place;
        }

    }

    Placement Place(const char *screen, const char *id, const Placement &defaults) {
        const auto *properties = theme::FindComponent(screen, id);
        return properties != nullptr ? Apply(*properties, defaults) : defaults;
    }

    double TextX(const Placement &place) {
        switch (place.align) {
            case Align::Center: return place.x + place.w / 2;
            case Align::Right:  return place.x + place.w;
            default:            return place.x;
        }
    }

    std::vector<Free> FreeComponents(const char *screen) {
        constexpr std::pair<std::string_view, Kind> Kinds[] = {
            { "clock", Kind::Clock }, { "battery", Kind::Battery }, { "text", Kind::Text }, { "image", Kind::Image },
            { "cover", Kind::Cover }, { "box", Kind::Box }, { "icon", Kind::Icon },
        };
        std::vector<Free> components;
        for (const auto &[id, properties] : theme::ScreenComponents(screen)) {
            const std::string *type = Property(properties, "Type");
            if (type == nullptr) {
                continue;
            }
            const auto kind = std::find_if(std::begin(Kinds), std::end(Kinds), [type](const auto &entry) { return *type == entry.first; });
            if (kind == std::end(Kinds)) {
                continue;
            }
            Free component;
            component.id = id;
            component.kind = kind->second;
            const bool has_text = kind->second == Kind::Clock || kind->second == Kind::Battery || kind->second == Kind::Text;
            component.place = Apply(properties, { true, 0, 0, has_text ? 300.0 : 200.0, has_text ? 30.0 : 200.0, has_text ? 20u : 0u, Align::Left });
            if (const std::string *text = Property(properties, "Text"); text != nullptr) {
                component.text = *text;
            }
            if (const std::string *image = Property(properties, "Image"); image != nullptr) {
                component.image = theme::ThemeAsset(*image);
            }
            if (const std::string *color = Property(properties, "Color"); color != nullptr) {
                component.has_color = ParseColor(*color, &component.color);
            }
            if (component.place.visible && (component.kind != Kind::Image || !component.image.empty())) {
                components.push_back(std::move(component));
            }
        }
        return components;
    }

}
