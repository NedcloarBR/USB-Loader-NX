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
#include <ui/draw.hpp>
#include <ui/motion.hpp>
#include <SDL2/SDL2_gfxPrimitives.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

namespace ui::draw {

    namespace {

        SDL_Renderer *R() {
            return pu::ui::render::GetMainRenderer();
        }

        double g_offset_x = 0, g_offset_y = 0;

        /* Lengths. */
        s16 P(double design_px) {
            return static_cast<s16>(S(design_px));
        }

        /* Positions, moved by the current Translate. */
        s16 PX(double design_x) {
            return P(design_x + g_offset_x);
        }

        s16 PY(double design_y) {
            return P(design_y + g_offset_y);
        }

        /* The theme's picture for an icon, centered and `size` wide (its own aspect kept); false without one. */
        bool ThemeIcon(theme::Icon icon, double cx, double cy, double size, Color c) {
            SDL_Texture *texture = theme::GetIcon(icon);
            if (texture == nullptr) {
                return false;
            }
            s32 width = 0, height = 0;
            SDL_QueryTexture(texture, nullptr, nullptr, &width, &height);
            const double h = width > 0 ? size * height / width : size;
            if (theme::TintsIcons()) {
                SDL_SetTextureColorMod(texture, c.r, c.g, c.b);
            }
            const SDL_Rect rect = { PX(cx - size / 2), PY(cy - h / 2), S(size), S(h) };
            SDL_SetTextureAlphaMod(texture, c.a);
            SDL_RenderCopy(R(), texture, nullptr, &rect);
            SDL_SetTextureAlphaMod(texture, 0xFF);
            SDL_SetTextureColorMod(texture, 0xFF, 0xFF, 0xFF);
            return true;
        }

        /* Antialiased shapes: built as triangles in screen px, with a 1 px rim fading to transparent on every edge. */
        struct Point {
            float x, y;
        };

        constexpr float Feather = 1.0f;

        Point ToScreen(double design_x, double design_y) {
            return { static_cast<float>((design_x + g_offset_x) * Scale), static_cast<float>((design_y + g_offset_y) * Scale) };
        }

        SDL_Vertex MakeVertex(Point p, Color c, u8 alpha) {
            return { { p.x, p.y }, { c.r, c.g, c.b, alpha }, { 0, 0 } };
        }

        void Submit(const std::vector<SDL_Vertex> &vertices, const std::vector<int> &indices) {
            SDL_SetRenderDrawBlendMode(R(), SDL_BLENDMODE_BLEND);
            SDL_RenderGeometry(R(), nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
        }

        /* Outward unit normal at each vertex of a closed convex path, whatever its winding. */
        std::vector<Point> Normals(const std::vector<Point> &path) {
            Point center = { 0, 0 };
            for (const auto &p : path) {
                center.x += p.x / path.size();
                center.y += p.y / path.size();
            }
            const auto unit = [](float x, float y) {
                const float length = std::sqrt(x * x + y * y);
                return length > 0 ? Point{ x / length, y / length } : Point{ 0, 0 };
            };

            std::vector<Point> normals;
            const size_t count = path.size();
            for (size_t i = 0; i < count; ++i) {
                const Point &prev = path[(i + count - 1) % count], &p = path[i], &next = path[(i + 1) % count];
                const Point a = unit(p.y - prev.y, prev.x - p.x), b = unit(next.y - p.y, p.x - next.x);
                Point n = unit(a.x + b.x, a.y + b.y);
                if (n.x * (p.x - center.x) + n.y * (p.y - center.y) < 0) {
                    n = { -n.x, -n.y };
                }
                normals.push_back(n);
            }
            return normals;
        }

        /* Rings of the path pushed out by each offset (screen px), joined by quads; alphas per ring. */
        void Bands(const std::vector<Point> &path, const std::vector<std::pair<float, u8>> &rings, Color c, std::vector<SDL_Vertex> *vertices, std::vector<int> *indices) {
            const auto normals = Normals(path);
            const int count = static_cast<int>(path.size()), base = static_cast<int>(vertices->size());
            for (const auto &[offset, alpha] : rings) {
                for (int i = 0; i < count; ++i) {
                    vertices->push_back(MakeVertex({ path[i].x + normals[i].x * offset, path[i].y + normals[i].y * offset }, c, alpha));
                }
            }
            for (int ring = 0; ring + 1 < static_cast<int>(rings.size()); ++ring) {
                for (int i = 0; i < count; ++i) {
                    const int a = base + ring * count + i, b = base + ring * count + (i + 1) % count;
                    const int c2 = a + count, d = b + count;
                    indices->insert(indices->end(), { a, c2, d, a, d, b });
                }
            }
        }

        /* Convex shapes, and any shape whose whole outline is visible from its centroid (a star). */
        void FillConvex(const std::vector<Point> &path, Color c) {
            if (path.size() < 3) {
                return;
            }
            std::vector<SDL_Vertex> vertices;
            std::vector<int> indices;
            Point center = { 0, 0 };
            for (const auto &p : path) {
                center.x += p.x / path.size();
                center.y += p.y / path.size();
            }
            vertices.push_back(MakeVertex(center, c, c.a));
            Bands(path, { { -Feather / 2, c.a }, { Feather / 2, 0 } }, c, &vertices, &indices);
            const int count = static_cast<int>(path.size());
            for (int i = 0; i < count; ++i) {
                indices.insert(indices.end(), { 0, 1 + i, 1 + (i + 1) % count });
            }
            Submit(vertices, indices);
        }

        void StrokeClosed(const std::vector<Point> &path, float width, Color c) {
            if (path.size() < 3) {
                return;
            }
            const float half = std::max(width, Feather) / 2;
            std::vector<SDL_Vertex> vertices;
            std::vector<int> indices;
            Bands(path, { { -half - Feather / 2, 0 }, { -half + Feather / 2, c.a }, { half - Feather / 2, c.a }, { half + Feather / 2, 0 } }, c, &vertices, &indices);
            Submit(vertices, indices);
        }

        /* Corners as arcs, finer for bigger radii; no repeated points. */
        std::vector<Point> RoundedRectPath(double x, double y, double w, double h, double radius) {
            radius = std::clamp(radius, 0.0, std::min(w, h) / 2);
            const int steps = std::clamp(static_cast<int>(radius * Scale / 2), 1, 16);
            const double corners[4][3] = { { x + w - radius, y + radius, -90 }, { x + w - radius, y + h - radius, 0 }, { x + radius, y + h - radius, 90 }, { x + radius, y + radius, 180 } };
            std::vector<Point> path;
            for (const auto &[cx, cy, start] : corners) {
                for (int i = 0; i <= (radius > 0 ? steps : 0); ++i) {
                    const double angle = (start + 90.0 * i / steps) * M_PI / 180;
                    const Point p = ToScreen(cx + radius * std::cos(angle), cy + radius * std::sin(angle));
                    if (path.empty() || std::abs(p.x - path.back().x) + std::abs(p.y - path.back().y) > 0.05f) {
                        path.push_back(p);
                    }
                }
            }
            return path;
        }

        std::vector<Point> ArcPath(double cx, double cy, double r, double from_degrees, double to_degrees, std::vector<Point> path = {}) {
            const int steps = std::clamp(static_cast<int>(r * Scale * std::abs(to_degrees - from_degrees) / 180), 6, 96);
            for (int i = 0; i <= steps; ++i) {
                const double angle = (from_degrees + (to_degrees - from_degrees) * i / steps) * M_PI / 180;
                path.push_back(ToScreen(cx + r * std::cos(angle), cy + r * std::sin(angle)));
            }
            return path;
        }

    }

    Translate::Translate(double dx, double dy) : previous_x(g_offset_x), previous_y(g_offset_y) {
        g_offset_x += dx;
        g_offset_y += dy;
    }

    Translate::~Translate() {
        g_offset_x = this->previous_x;
        g_offset_y = this->previous_y;
    }

    void Rect(double x, double y, double w, double h, Color c) {
        boxRGBA(R(), PX(x), PY(y), PX(x + w) - 1, PY(y + h) - 1, c.r, c.g, c.b, c.a);
    }

    void RoundRect(double x, double y, double w, double h, double radius, Color c) {
        FillConvex(RoundedRectPath(x, y, w, h, radius), c);
    }

    void RoundOutline(double x, double y, double w, double h, double radius, double thickness, Color c) {
        const double inset = thickness / 2;
        StrokeClosed(RoundedRectPath(x + inset, y + inset, w - thickness, h - thickness, std::max(0.0, radius - inset)), static_cast<float>(thickness * Scale), c);
    }

    void FrameRect(double x, double y, double w, double h, double radius, double thickness, Color border, Color fill) {
        RoundRect(x, y, w, h, radius, border);
        RoundRect(x + thickness, y + thickness, w - 2 * thickness, h - 2 * thickness, std::max(0.0, radius - thickness), fill);
    }

    void Circle(double cx, double cy, double r, Color c) {
        auto path = ArcPath(cx, cy, r, 0, 360);
        path.pop_back();
        FillConvex(path, c);
    }

    void Ring(double cx, double cy, double r, double thickness, Color c, Color fill) {
        Circle(cx, cy, r, c);
        Circle(cx, cy, r - thickness, fill);
    }

    void Line(double x1, double y1, double x2, double y2, double width, Color c) {
        /* A capsule: half a circle around each end, so the caps are round. */
        const double angle = std::atan2(y2 - y1, x2 - x1) * 180 / M_PI, r = width / 2;
        auto path = ArcPath(x2, y2, r, angle - 90, angle + 90);
        path = ArcPath(x1, y1, r, angle + 90, angle + 270, std::move(path));
        FillConvex(path, c);
    }

    void Triangle(double x1, double y1, double x2, double y2, double x3, double y3, Color c) {
        FillConvex({ ToScreen(x1, y1), ToScreen(x2, y2), ToScreen(x3, y3) }, c);
    }

    void Image(SDL_Texture *texture, double x, double y, double w, double h, u8 alpha) {
        if (texture == nullptr) {
            return;
        }
        const SDL_Rect rect = { PX(x), PY(y), S(w), S(h) };
        SDL_SetTextureAlphaMod(texture, alpha);
        SDL_RenderCopy(R(), texture, nullptr, &rect);
        SDL_SetTextureAlphaMod(texture, 0xFF);
    }

    void ImagePart(SDL_Texture *texture, const SDL_Rect &source, double x, double y, double w, double h, u8 alpha) {
        if (texture == nullptr || source.w <= 0 || source.h <= 0) {
            return;
        }
        const SDL_Rect rect = { PX(x), PY(y), S(w), S(h) };
        SDL_SetTextureAlphaMod(texture, alpha);
        SDL_RenderCopy(R(), texture, &source, &rect);
        SDL_SetTextureAlphaMod(texture, 0xFF);
    }

    void ImageTurned(SDL_Texture *texture, double cx, double cy, double w, double h, double degrees, u8 alpha) {
        if (texture == nullptr) {
            return;
        }
        const SDL_Rect rect = { PX(cx - w / 2), PY(cy - h / 2), S(w), S(h) };
        SDL_SetTextureAlphaMod(texture, alpha);
        SDL_RenderCopyEx(R(), texture, nullptr, &rect, degrees, nullptr, SDL_FLIP_NONE);
        SDL_SetTextureAlphaMod(texture, 0xFF);
    }

    void ImageQuad(SDL_Texture *texture, const Corner (&corners)[4], bool flip, double span) {
        if (texture == nullptr) {
            return;
        }
        const float top = flip ? 1.0f : 0.0f, bottom = flip ? static_cast<float>(1.0 - std::clamp(span, 0.0, 1.0)) : 1.0f;
        const float u[] = { 0.0f, 1.0f, 1.0f, 0.0f }, v[] = { top, top, bottom, bottom };
        SDL_Vertex vertices[4];
        for (int i = 0; i < 4; ++i) {
            const Point p = ToScreen(corners[i].x, corners[i].y);
            vertices[i] = { { p.x, p.y }, { 0xFF, 0xFF, 0xFF, corners[i].alpha }, { u[i], v[i] } };
        }
        const int indices[] = { 0, 1, 2, 0, 2, 3 };
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_RenderGeometry(R(), texture, vertices, 4, indices, 6);
    }

    SDL_Texture *MakeBlurred(SDL_Texture *source) {
        constexpr int Width = 32, Height = 18;
        int sw = 0, sh = 0;
        if (source == nullptr || SDL_QueryTexture(source, nullptr, nullptr, &sw, &sh) != 0 || sw <= 0 || sh <= 0) {
            return nullptr;
        }
        SDL_Texture *target = SDL_CreateTexture(R(), SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, Width, Height);
        if (target == nullptr) {
            return nullptr;
        }
        /* The middle band of the picture, as wide as the screen's shape allows. */
        SDL_Rect crop = { 0, 0, sw, sh };
        if (sw * Height > sh * Width) {
            crop.w = sh * Width / Height;
            crop.x = (sw - crop.w) / 2;
        } else {
            crop.h = sw * Height / Width;
            crop.y = (sh - crop.h) / 2;
        }
        SDL_Texture *previous = SDL_GetRenderTarget(R());
        SDL_SetRenderTarget(R(), target);
        SDL_SetRenderDrawColor(R(), 0, 0, 0, 0xFF);
        SDL_RenderClear(R());
        SDL_SetTextureScaleMode(source, SDL_ScaleModeLinear);
        SDL_RenderCopy(R(), source, &crop, nullptr);
        SDL_SetRenderTarget(R(), previous);
        SDL_SetTextureScaleMode(target, SDL_ScaleModeLinear);
        SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);
        return target;
    }

    void NineSlice(SDL_Texture *texture, double border, double x, double y, double w, double h, u8 alpha) {
        if (texture == nullptr) {
            return;
        }
        s32 tw = 0, th = 0;
        SDL_QueryTexture(texture, nullptr, nullptr, &tw, &th);
        /* Source corners in texture px, drawn at the same size in screen px; both kept within half of each side. */
        const s32 sb = std::clamp<s32>(static_cast<s32>(border), 0, std::min(tw, th) / 2);
        const s32 left = PX(x), top = PY(y), right = PX(x + w), bottom = PY(y + h);
        const s32 db = std::min({ sb, (right - left) / 2, (bottom - top) / 2 });
        const s32 sx[] = { 0, sb, tw - sb, tw }, sy[] = { 0, sb, th - sb, th };
        const s32 dx[] = { left, left + db, right - db, right }, dy[] = { top, top + db, bottom - db, bottom };
        SDL_SetTextureAlphaMod(texture, alpha);
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                const SDL_Rect src = { sx[column], sy[row], sx[column + 1] - sx[column], sy[row + 1] - sy[row] };
                const SDL_Rect dst = { dx[column], dy[row], dx[column + 1] - dx[column], dy[row + 1] - dy[row] };
                if (src.w > 0 && src.h > 0 && dst.w > 0 && dst.h > 0) {
                    SDL_RenderCopy(R(), texture, &src, &dst);
                }
            }
        }
        SDL_SetTextureAlphaMod(texture, 0xFF);
    }

    bool Skinned(theme::Skin skin, double x, double y, double w, double h, u8 alpha) {
        const theme::SkinImage *image = theme::GetSkin(skin);
        if (image == nullptr) {
            return false;
        }
        NineSlice(image->texture, image->border, x, y, w, h, alpha);
        return true;
    }

    void Ambient(double seconds) {
        constexpr s32 Count = 28;
        for (s32 i = 0; i < Count; ++i) {
            /* A fixed pseudo-random lane, speed and size per speck. */
            const double a = std::fmod(std::sin(i * 12.9898) * 43758.5453, 1.0), b = std::fmod(std::sin(i * 78.233) * 12345.678, 1.0);
            const double lane = std::abs(a), phase = std::abs(b);
            const double speed = 10 + 16 * phase, radius = 1.6 + 2.2 * lane;
            const double travel = DesignHeight + 80;
            const double y = DesignHeight + 40 - std::fmod(seconds * speed + phase * travel, travel);
            const double x = lane * DesignWidth + std::sin(seconds * 0.4 + i) * 18;
            const double twinkle = 0.5 + 0.5 * std::sin(seconds * (1.2 + phase) + i * 1.7);
            const Color base = i % 3 == 0 ? color::Info : color::Primary;
            Circle(x, y, radius, color::WithAlpha(base, static_cast<u8>(18 + 46 * twinkle)));
        }
    }

    void Background() {
        if (SDL_Texture *image = theme::BackgroundImage(); image != nullptr) {
            s32 width = 0, height = 0;
            SDL_QueryTexture(image, nullptr, nullptr, &width, &height);
            if (theme::IsBackgroundTiled() && width > 0 && height > 0) {
                const double w = width / Scale, h = height / Scale;
                for (double y = 0; y < DesignHeight; y += h) {
                    for (double x = 0; x < DesignWidth; x += w) {
                        Image(image, x, y, w, h);
                    }
                }
            } else {
                Image(image, 0, 0, DesignWidth, DesignHeight);
            }
            Rect(0, 0, DesignWidth, DesignHeight, color::WithAlpha(color::Ground, static_cast<u8>(theme::BackgroundDim() * 255)));
        }
        if (theme::HasParticles()) {
            Ambient(Seconds());
        }
    }

    Color Mix(Color from, Color to, double t) {
        const auto mix = [t](u8 a, u8 b) { return static_cast<u8>(std::lround(a + (b - a) * t)); };
        return { mix(from.r, to.r), mix(from.g, to.g), mix(from.b, to.b), mix(from.a, to.a) };
    }

    void FocusRing(double x, double y, double w, double h, double radius, double gap) {
        constexpr double Width = 4;
        const double pulse = FocusPulse(), out = gap + Width;
        const Color c = Mix(color::Focus, color::FocusAlt, pulse);
        if (const theme::SkinImage *image = theme::GetSkin(theme::Skin::FocusRing); image != nullptr) {
            /* The theme's ring, as wide as the drawn one with its glow; it pulses in opacity. */
            if (theme::TintsIcons()) {
                SDL_SetTextureColorMod(image->texture, c.r, c.g, c.b);
            }
            NineSlice(image->texture, image->border, x - out - 8, y - out - 8, w + 2 * (out + 8), h + 2 * (out + 8), static_cast<u8>(0xC0 + 0x3F * pulse));
            SDL_SetTextureColorMod(image->texture, 0xFF, 0xFF, 0xFF);
            return;
        }

        /* The glow: from the ring's outer edge outwards, fading to nothing; it swells and brightens with the pulse. */
        const auto path = RoundedRectPath(x - out, y - out, w + 2 * out, h + 2 * out, radius + out);
        const float reach = static_cast<float>((20 + 14 * pulse) * Scale);
        const u8 alpha = static_cast<u8>(0x58 + 0x30 * pulse);
        std::vector<SDL_Vertex> vertices;
        std::vector<int> indices;
        Bands(path, { { 0, alpha }, { reach * 0.35f, static_cast<u8>(alpha * 0.45) }, { reach, 0 } }, c, &vertices, &indices);
        Submit(vertices, indices);

        RoundOutline(x - out, y - out, w + 2 * out, h + 2 * out, radius + out, Width, c);
    }

    Color UsageColor(double usage) {
        const std::pair<double, Color> stops[] = { { 0.0, color::Primary }, { 0.6, color::Primary }, { 0.75, color::Warning }, { 0.88, color::Pending }, { 1.0, color::Error } };
        usage = std::clamp(usage, 0.0, 1.0);
        for (size_t i = 1; i < std::size(stops); ++i) {
            if (usage <= stops[i].first) {
                return Mix(stops[i - 1].second, stops[i].second, (usage - stops[i - 1].first) / (stops[i].first - stops[i - 1].first));
            }
        }
        return color::Error;
    }

    void UsageBar(double x, double y, double w, double h, double usage) {
        const double r = h / 2;
        RoundRect(x, y, w, h, r, color::Track);
        const double fill = w * std::clamp(usage, 0.0, 1.0);
        if (fill <= 0) {
            return;
        }
        if (fill <= h) {
            Circle(x + r, y + r, r * std::max(0.5, fill / h), UsageColor(usage));
            return;
        }

        /* Round caps in the colors at both ends; between them a strip with a color column at every stop it crosses. */
        const double left = x + r, right = x + fill - r;
        std::vector<double> columns = { left };
        for (const double stop : { 0.6, 0.75, 0.88 }) {
            if (const double cx = x + w * stop; cx > left && cx < right) {
                columns.push_back(cx);
            }
        }
        columns.push_back(right);

        Circle(left, y + r, r, UsageColor((left - x) / w));
        Circle(right, y + r, r, UsageColor((right - x) / w));
        const float feather = Feather / 2;
        std::vector<SDL_Vertex> vertices;
        std::vector<int> indices;
        for (const double cx : columns) {
            const Color c = UsageColor((cx - x) / w);
            const Point top = ToScreen(cx, y), bottom = ToScreen(cx, y + h);
            vertices.push_back(MakeVertex({ top.x, top.y - feather }, c, 0));
            vertices.push_back(MakeVertex({ top.x, top.y + feather }, c, c.a));
            vertices.push_back(MakeVertex({ bottom.x, bottom.y - feather }, c, c.a));
            vertices.push_back(MakeVertex({ bottom.x, bottom.y + feather }, c, 0));
        }
        for (int column = 0; column + 1 < static_cast<int>(columns.size()); ++column) {
            for (int row = 0; row < 3; ++row) {
                const int a = column * 4 + row, b = a + 1, c = a + 4, d = b + 4;
                indices.insert(indices.end(), { a, c, d, a, d, b });
            }
        }
        Submit(vertices, indices);
    }

    void ButtonGlyph(double cx, double cy, double width) {
        RoundRect(cx - width / 2, cy - 12, width, 24, width > 24 ? 8 : 12, color::Text);
    }

    void IconUsbStick(double cx, double cy, double size, Color c) {
        if (ThemeIcon(theme::Icon::Usb, cx, cy, size, c)) {
            return;
        }
        const double s = size / 24.0, x = cx - size / 2, y = cy - size / 2, w = 1.8 * s;
        RoundOutline(x + 7 * s, y + 9 * s, 10 * s, 13 * s, 2 * s, w, c);
        RoundOutline(x + 9 * s, y + 3 * s, 6 * s, 6.9 * s, 0.6 * s, w, c);
        Circle(x + 11 * s, y + 5.6 * s, 0.75 * s, c);
        Circle(x + 13 * s, y + 5.6 * s, 0.75 * s, c);
    }

    void IconClock(double cx, double cy, double size, Color c) {
        if (ThemeIcon(theme::Icon::Clock, cx, cy, size, c)) {
            return;
        }
        const double s = size / 24.0, w = 2 * s;
        RoundOutline(cx - 9 * s - w / 2, cy - 9 * s - w / 2, 18 * s + w, 18 * s + w, 9 * s + w / 2, w, c);
        Line(cx, cy - 5 * s, cx, cy, w, c);
        Line(cx, cy, cx + 3 * s, cy + 2 * s, w, c);
    }

    void IconWarning(double cx, double cy, double size, Color c) {
        if (ThemeIcon(theme::Icon::Warning, cx, cy, size, c)) {
            return;
        }
        const double s = size / 24.0, x = cx - size / 2, y = cy - size / 2, w = 2 * s;
        StrokeClosed({ ToScreen(x + 12 * s, y + 3 * s), ToScreen(x + 21.5 * s, y + 20 * s), ToScreen(x + 2.5 * s, y + 20 * s) }, static_cast<float>(w * Scale), c);
        Line(x + 12 * s, y + 10 * s, x + 12 * s, y + 14 * s, w, c);
        Circle(x + 12 * s, y + 17.3 * s, 1.1 * s, c);
    }

    void Chevron(double cx, double cy, double size, Color c) {
        if (ThemeIcon(theme::Icon::Chevron, cx, cy, size, c)) {
            return;
        }
        const double s = size / 24.0, w = 2.4 * s;
        Line(cx - 3.5 * s, cy - 7 * s, cx + 3.5 * s, cy, w, c);
        Line(cx + 3.5 * s, cy, cx - 3.5 * s, cy + 7 * s, w, c);
    }

    void ChevronLeft(double cx, double cy, double size, Color c) {
        const double s = size / 24.0, w = 2.4 * s;
        Line(cx + 3.5 * s, cy - 7 * s, cx - 3.5 * s, cy, w, c);
        Line(cx - 3.5 * s, cy, cx + 3.5 * s, cy + 7 * s, w, c);
    }

    void ChevronVertical(double cx, double cy, double size, bool down, Color c) {
        const double s = size / 24.0, w = 2.4 * s, tip = down ? 3.5 * s : -3.5 * s;
        Line(cx - 7 * s, cy - tip, cx, cy + tip, w, c);
        Line(cx, cy + tip, cx + 7 * s, cy - tip, w, c);
    }

    void Star(double cx, double cy, double r, Color c) {
        if (ThemeIcon(theme::Icon::Star, cx, cy, r * 2, c)) {
            return;
        }
        /* One polygon, points and notches alternating; every vertex is visible from the center, so the fan fill holds. */
        std::vector<Point> path;
        for (int i = 0; i < 10; ++i) {
            const double angle = (-90 + 36 * i) * M_PI / 180, radius = i % 2 == 0 ? r : r * 0.42;
            path.push_back(ToScreen(cx + radius * std::cos(angle), cy + radius * std::sin(angle)));
        }
        FillConvex(path, c);
    }

    void IconPackage(double cx, double cy, double size, Color c) {
        if (ThemeIcon(theme::Icon::Package, cx, cy, size, c)) {
            return;
        }
        const double w = size * 0.74, h = size * 0.6, x = cx - w / 2, y = cy - h / 2 + size * 0.06, line = size * 0.1;
        RoundOutline(x, y, w, h, size * 0.08, line, c);
        Line(x - size * 0.04, y, x + w + size * 0.04, y, line, c);
        Line(cx, y, cx, y + h * 0.42, line, c);
    }

    void IconGrid(double cx, double cy, double size, Color c) {
        if (ThemeIcon(theme::Icon::Grid, cx, cy, size, c)) {
            return;
        }
        const double s = size / 24.0, x = cx - size / 2, y = cy - size / 2;
        for (const auto &[gx, gy] : { std::pair{ 3.5, 4.5 }, std::pair{ 13.5, 4.5 }, std::pair{ 3.5, 13.5 }, std::pair{ 13.5, 13.5 } }) {
            RoundRect(x + gx * s, y + gy * s, 7 * s, 6 * s, 1.5 * s, c);
        }
    }

    void GameCard(double x, double y, double w, double h, Color body, Color grip) {
        RoundRect(x, y, w, h, w * 0.09, body);
        RoundRect(x + w / 2 - w * 0.12, y + h * 0.035, w * 0.24, h * 0.027, h * 0.013, grip);
    }

}
