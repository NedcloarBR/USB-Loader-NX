#!/usr/bin/env python3
"""Regenerates manager/icon.jpg, the Manager's 256x256 HOME icon, from the geometry of assets/icon.svg.

Also copied to manager/romfs/icon.jpg for the About page. The JPEG is written by rgb_to_jpg.c (stb_image_write), built with the host's cc in a temporary folder."""
import math
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usbl_shapes as S

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'manager', 'icon.jpg')
# The same icon, for the Manager's About page.
ROMFS_COPY = os.path.join(HERE, '..', '..', 'manager', 'romfs', 'icon.jpg')
SIZE = 256

GROUND = (0x1E, 0x1F, 0x22)
RAISED = (0x35, 0x37, 0x3C)
PLUG = (0xDB, 0xDE, 0xE1)
PANEL = S.COLORS['default']
PRIMARY = S.COLORS['primary']

def rotate(points, degrees, cx, cy):
    a = math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    return [(cx + (x - cx) * c - (y - cy) * s, cy + (x - cx) * s + (y - cy) * c) for x, y in points]

def cubic(p0, p1, p2, p3, n=24):
    return [tuple((1 - t) ** 3 * a + 3 * (1 - t) ** 2 * t * b + 3 * (1 - t) * t ** 2 * c + t ** 3 * d for a, b, c, d in zip(p0, p1, p2, p3))
            for t in (i / n for i in range(n + 1))]

def ribbon(points, width):
    """A polyline stroked as one polygon with round caps, so a translucent stroke has no overlaps."""
    left, right = [], []
    for i, (x, y) in enumerate(points):
        (ax, ay), (bx, by) = points[max(0, i - 1)], points[min(len(points) - 1, i + 1)]
        length = math.hypot(bx - ax, by - ay) or 1
        nx, ny = -(by - ay) / length * width / 2, (bx - ax) / length * width / 2
        left.append((x + nx, y + ny))
        right.append((x - nx, y - ny))

    def cap(center, towards):
        angle = math.atan2(towards[1] - center[1], towards[0] - center[0])
        return [(center[0] + width / 2 * math.cos(angle + math.pi / 2 + math.pi * k / 8), center[1] + width / 2 * math.sin(angle + math.pi / 2 + math.pi * k / 8)) for k in range(1, 8)]

    return left + cap(points[-1], points[-2]) + right[::-1] + cap(points[0], points[1])

def scene():
    back_card = S.rrect(72, 44, 112, 152, 16)
    arc = cubic((30, 222), (64, 222), (82, 208), (128, 208)) + cubic((128, 208), (174, 208), (192, 222), (226, 222))[1:]
    return [
        # The HOME icon is shown square: the background fills the corners the SVG rounds.
        ([[(0, 0), (SIZE, 0), (SIZE, SIZE), (0, SIZE)]], PANEL, 1.0),
        ([rotate(back_card, -17, 128, 236), rotate(back_card, 17, 128, 236)], RAISED, 1.0),
        ([ribbon(arc, 5)], PRIMARY, 0.55),
        ([S.rrect(100, 176, 56, 40, 6)], PLUG, 1.0),
        ([S.rrect(110, 190, 12, 10, 2), S.rrect(134, 190, 12, 10, 2)], PANEL, 1.0),
        ([S.rrect(72, 36, 112, 144, 14)], PRIMARY, 1.0),
        ([ribbon([(106, 46), (150, 46)], 5)], GROUND, 0.35),
        ([S.rrect(86, 60, 84, 100, 10)], GROUND, 1.0),
        # The USB trident: every stroke a separate round-capped segment (opaque, so overlaps do not show), the arrow
        # sitting on the stem, the branches leaving it at 45 degrees and ending in a dot and a square.
        ([ribbon([(128, 78), (128, 140)], 7),
          ribbon([(128, 132), (110, 117)], 7), ribbon([(110, 117), (110, 106)], 7),
          ribbon([(128, 121), (146, 106)], 7), ribbon([(146, 106), (146, 99)], 7),
          [(128, 62), (118, 79), (138, 79)],
          S.circle(110, 101, 6.5), S.rrect(140, 88, 12, 12, 2.5), S.circle(128, 143, 7.5)], PRIMARY, 1.0),
    ]

def main():
    rgba = S.rasterize(scene(), SIZE, SIZE)
    rgb = bytes(b for i in range(0, len(rgba), 4) for b in rgba[i:i + 3])
    with tempfile.TemporaryDirectory() as tmp:
        tool = os.path.join(tmp, 'rgb_to_jpg')
        subprocess.run(['cc', '-O2', '-o', tool, os.path.join(HERE, 'rgb_to_jpg.c'), '-lm'], check=True)
        subprocess.run([tool, str(SIZE), str(SIZE), '95', os.path.abspath(OUT)], input=rgb, check=True)
    os.makedirs(os.path.dirname(ROMFS_COPY), exist_ok=True)
    shutil.copyfile(OUT, ROMFS_COPY)
    print(f'icon.jpg: {os.path.getsize(OUT)} bytes')

if __name__ == '__main__':
    main()
