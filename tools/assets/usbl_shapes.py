"""Shared geometry for USB Loader NX assets: emits SVG for previews and rasterizes to RGBA8888.

Colours are the N-D-B palette (N-D-B src/types/Colors.ts)."""
import math

COLORS = {
    'primary': (0x00, 0xC2, 0x6F),
    'info':    (0x00, 0xB8, 0xC2),
    'warning': (0xC2, 0xB2, 0x00),
    'pending': (0xC2, 0x78, 0x00),
    'error':   (0xC2, 0x0E, 0x00),
    'default': (0x2B, 0x2D, 0x31),
    'text':    (0xF2, 0xF3, 0xF5),
}

def hexc(c):
    return '#%02X%02X%02X' % c

def rrect(x, y, w, h, r, k=6):
    pts = []
    for cx, cy, a0 in ((x + w - r, y + r, -90), (x + w - r, y + h - r, 0), (x + r, y + h - r, 90), (x + r, y + r, 180)):
        for i in range(k + 1):
            a = math.radians(a0 + 90 * i / k)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts

def circle(cx, cy, r, n=40):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]

def stroke(points, w):
    """Polyline as a set of quads plus round joins (list of polygons)."""
    polys = []
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        dx, dy = x1 - x0, y1 - y0
        l = math.hypot(dx, dy) or 1
        nx, ny = -dy / l * w / 2, dx / l * w / 2
        polys.append([(x0 + nx, y0 + ny), (x1 + nx, y1 + ny), (x1 - nx, y1 - ny), (x0 - nx, y0 - ny)])
    for p in points:
        polys.append(circle(p[0], p[1], w / 2, 16))
    return polys

def usb_glyph(ox, oy, s):
    """USB trident in a 50x50 box, scaled by s and offset."""
    T = lambda pts: [(ox + x * s, oy + y * s) for x, y in pts]
    polys = []
    polys += stroke(T([(25, 12), (25, 37)]), 3.6 * s)
    polys.append(T([(25, 5.5), (19.8, 13.5), (30.2, 13.5)]))
    polys.append(T(circle(25, 40.5, 4.6)))
    polys += stroke(T([(25, 30), (16.5, 24.5), (16.5, 19)]), 3.0 * s)
    polys.append(T(circle(16.5, 16.8, 3.3)))
    polys += stroke(T([(25, 34), (33.5, 28.5), (33.5, 22.5)]), 3.0 * s)
    polys.append(T([(30.6, 16.4), (36.4, 16.4), (36.4, 22.2), (30.6, 22.2)]))
    return polys

def notif_icon(state):
    col = COLORS[state]
    return [([rrect(0, 0, 50, 50, 12)], COLORS['default'], 1.0),
            ([rrect(1, 1, 48, 48, 11)], col, 0.10),
            (usb_glyph(0, 0, 1.0), col, 1.0)]

def to_svg(scene, W, H, extra=''):
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}">', extra]
    for polys, col, a in scene:
        d = ' '.join('M' + ' L'.join(f'{px:.2f},{py:.2f}' for px, py in p) + ' Z' for p in polys)
        out.append(f'<path d="{d}" fill="{hexc(col)}" fill-opacity="{a}" fill-rule="nonzero"/>')
    out.append('</svg>')
    return ''.join(out)

def rasterize(scene, W, H, base=None, ss=4):
    """Returns RGBA8888 bytes; polygons filled with nonzero winding, ss x ss supersampling."""
    img = bytearray(base) if base else bytearray(W * H * 4)
    for polys, col, a in scene:
        cov = [0.0] * (W * H)
        for sy in range(H * ss):
            yy = (sy + 0.5) / ss
            xs = []
            for p in polys:
                for (x0, y0), (x1, y1) in zip(p, p[1:] + p[:1]):
                    if (y0 <= yy < y1) or (y1 <= yy < y0):
                        xs.append((x0 + (yy - y0) * (x1 - x0) / (y1 - y0), 1 if y1 > y0 else -1))
            xs.sort()
            wind, start = 0, None
            row = sy // ss
            for xv, d in xs:
                prev = wind
                wind += d
                if prev == 0 and wind != 0:
                    start = xv
                elif prev != 0 and wind == 0 and start is not None:
                    for sx in range(max(0, int(start * ss)), min(W * ss, int(xv * ss + 1))):
                        cx = (sx + 0.5) / ss
                        if start <= cx < xv:
                            cov[row * W + sx // ss] += 1.0 / (ss * ss)
        for i, c in enumerate(cov):
            if c <= 0:
                continue
            sa = min(1.0, c) * a
            o = i * 4
            da = img[o + 3] / 255.0
            oa = sa + da * (1 - sa)
            for k in range(3):
                dc = img[o + k] / 255.0
                v = (col[k] / 255.0 * sa + dc * da * (1 - sa)) / oa if oa > 0 else 0
                img[o + k] = int(round(v * 255))
            img[o + 3] = int(round(oa * 255))
    return bytes(img)

def gamecard_silhouette(ox, oy, s):
    """Switch game card silhouette in a 50x50 box: 21:31 portrait, evenly rounded corners."""
    T = lambda pts: [(ox + x * s, oy + y * s) for x, y in pts]
    return [T(rrect(14, 8, 22.5, 33.5, 3.0))]

def gamecard_details(ox, oy, s):
    """Grip groove and label outline, cut out in the background colour over the silhouette."""
    T = lambda pts: [(ox + x * s, oy + y * s) for x, y in pts]
    label = rrect(18, 20, 14.5, 17.5, 2.0)
    return [T(rrect(18, 13.2, 14.5, 2.2, 1.1))] + stroke(T(label + label[:1]), 1.6 * s)

def package_glyph(ox, oy, s):
    """Box with a closed lid seam and a tape strip, in a 50x50 box."""
    T = lambda pts: [(ox + x * s, oy + y * s) for x, y in pts]
    polys = [T(rrect(9, 15, 32, 27, 3.0))]
    polys.append(T([(11, 9), (39, 9), (41, 15), (9, 15)]))
    return polys

def package_details(ox, oy, s):
    T = lambda pts: [(ox + x * s, oy + y * s) for x, y in pts]
    polys = [T([(22, 9), (28, 9), (28, 25), (25, 22.5), (22, 25)])]
    polys += stroke(T([(9, 15.6), (41, 15.6)]), 1.6 * s)
    return polys




def blocks_glyph(ox, oy, s):
    """Four stacked blocks, one dropping into place, in a 50x50 box."""
    T = lambda pts: [(ox + x * s, oy + y * s) for x, y in pts]
    polys = [T(rrect(8, 26, 15, 15, 2.6)), T(rrect(27, 26, 15, 15, 2.6)), T(rrect(8, 7, 15, 15, 2.6))]
    polys.append(T(rrect(27.5, 4, 15, 15, 2.6)))
    return polys

def badge_usb_source(glyph, details=None, W=256, H=256, z_mark=False):
    """USB trident + a source glyph (game card, NSP, installed) on the dark pill; z_mark adds a Z bubble on its top
    right corner for a compressed XCZ or NSZ."""
    bw, bh, m = 98, 44, 10
    x, y = W - bw - m, H - bh - m
    sep_x = x + bw / 2
    gx, gy, gs = sep_x + 1, y + 3, 0.76
    scene = [([rrect(x - 2.5, y - 2.5, bw + 5, bh + 5, (bh + 5) / 2)], COLORS['primary'], 1.0),
             ([rrect(x, y, bw, bh, bh / 2)], COLORS['default'], 1.0),
             (usb_glyph(x + 9, y + 3, gs), COLORS['primary'], 1.0),
             ([rrect(sep_x - 0.9, y + 11, 1.8, bh - 22, 0.9)], COLORS['text'], 0.25),
             (glyph(gx, gy, gs), COLORS['text'], 1.0)]
    if details is not None:
        scene.append((details(gx, gy, gs), COLORS['default'], 1.0))
    if z_mark:
        cx, cy, r = x + bw - 6, y + 5, 10.5
        z = [(-4.6, -5.2), (4.6, -5.2), (4.6, -3.0), (-1.4, 3.0), (4.6, 3.0), (4.6, 5.2), (-4.6, 5.2), (-4.6, 3.0), (1.4, -3.0), (-4.6, -3.0)]
        scene += [([circle(cx, cy, r + 2.5)], COLORS['default'], 1.0),
                  ([circle(cx, cy, r)], COLORS['primary'], 1.0),
                  ([[(cx + dx, cy + dy) for dx, dy in z]], COLORS['default'], 1.0)]
    return scene
