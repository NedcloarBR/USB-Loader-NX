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
#include <ui/widgets.hpp>
#include <vector>

namespace ui {

    namespace {

        bool IsUtf8Continuation(char c) {
            return (static_cast<u8>(c) & 0xC0) == 0x80;
        }

    }

    std::string Fit(const std::string &text, u32 size, double max_width) {
        const std::string font = Font(size);
        const s32 limit = S(max_width);
        if (max_width <= 0 || pu::ui::render::GetTextWidth(font, text) <= limit) {
            return text;
        }

        /* Longest prefix, cut on a code point, that fits with the ellipsis. */
        std::vector<size_t> cuts;
        for (size_t i = 1; i < text.size(); ++i) {
            if (!IsUtf8Continuation(text[i])) {
                cuts.push_back(i);
            }
        }
        size_t low = 0, high = cuts.size();
        while (low < high) {
            const size_t middle = (low + high) / 2;
            if (pu::ui::render::GetTextWidth(font, text.substr(0, cuts[middle]) + "…") <= limit) {
                low = middle + 1;
            } else {
                high = middle;
            }
        }
        std::string fitted = low > 0 ? text.substr(0, cuts[low - 1]) : std::string();
        while (!fitted.empty() && fitted.back() == ' ') {
            fitted.pop_back();
        }
        return fitted + "…";
    }

    std::vector<std::string> WrapLines(const std::string &text, u32 size, double max_width, size_t max_lines) {
        const std::string font = Font(size);
        const s32 limit = S(max_width);
        std::vector<std::string> lines;
        std::string line;
        size_t position = 0;
        while (position < text.size()) {
            const size_t space = text.find(' ', position);
            const std::string word = text.substr(position, space == std::string::npos ? std::string::npos : space - position);
            position = space == std::string::npos ? text.size() : space + 1;

            const std::string candidate = line.empty() ? word : line + " " + word;
            if (line.empty() || pu::ui::render::GetTextWidth(font, candidate) <= limit) {
                line = candidate;
                continue;
            }
            if (lines.size() + 1 == max_lines) {
                /* Out of lines: the rest goes on the last one, cut with an ellipsis. */
                line = candidate + (position < text.size() ? " " + text.substr(position) : std::string());
                position = text.size();
                break;
            }
            lines.push_back(line);
            line = word;
        }
        if (!line.empty()) {
            lines.push_back(Fit(line, size, max_width));
        }
        return lines;
    }

    pu::ui::elm::TextBlock::Ref AddText(pu::ui::Layout &layout, double x, double y, const std::string &text, u32 size, pu::ui::Color color,
                                        Align align, double max_width) {
        auto block = pu::ui::elm::TextBlock::New(0, S(y), Fit(text, size, max_width));
        block->SetFont(Font(size));
        block->SetColor(color);

        const s32 width = block->GetWidth();
        switch (align) {
            case Align::Left:   block->SetX(S(x));             break;
            case Align::Center: block->SetX(S(x) - width / 2); break;
            case Align::Right:  block->SetX(S(x) - width);     break;
        }
        layout.Add(block);
        return block;
    }

    void AddPaint(pu::ui::Layout &layout, std::function<void()> paint) {
        layout.Add(draw::Painter::New(std::move(paint)));
    }

    pu::ui::elm::Image::Ref AddImage(pu::ui::Layout &layout, pu::sdl2::TextureHandle::Ref texture, double x, double y, double size) {
        auto image = pu::ui::elm::Image::New(S(x), S(y), texture);
        image->SetWidth(S(size));
        image->SetHeight(S(size));
        layout.Add(image);
        return image;
    }

    pu::ui::elm::TextBlock::Ref AddButtonHint(pu::ui::Layout &layout, double cx, double cy, const std::string &label) {
        auto text = pu::ui::elm::TextBlock::New(0, 0, label);
        text->SetFont(Font(13));
        text->SetColor(color::Ground);
        const double width = label.size() > 1 ? text->GetWidth() / Scale + 14 : 24;
        AddPaint(layout, [cx, cy, width]() { draw::ButtonGlyph(cx, cy, width); });
        text->SetX(S(cx) - text->GetWidth() / 2);
        text->SetY(S(cy) - text->GetHeight() / 2);
        layout.Add(text);
        return text;
    }

    double AddHintBar(pu::ui::Layout &layout, double right, double cy, const std::vector<Hint> &hints, pu::ui::Color text_color) {
        double x = right;
        for (const auto &hint : hints) {
            const auto text = AddText(layout, x, cy - 10, hint.text, 14, text_color, Align::Right);
            x -= text->GetWidth() / Scale + 8;
            const double glyph = hint.button.size() > 1 ? pu::ui::render::GetTextWidth(Font(13), hint.button) / Scale + 14 : 24;
            AddButtonHint(layout, x - glyph / 2, cy, hint.button);
            x -= glyph + 26;
        }
        return x;
    }

}
