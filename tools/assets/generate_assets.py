#!/usr/bin/env python3
"""Regenerates the embedded USB Loader NX assets in stratosphere/ams_mitm/source/usbl/assets/.

usbloadernx-<state>.rgba : 50x50 RGBA8888 Ultrahand notification icons.
usbloadernx-badge-<source>.rgba : the icon badge for games from an XCI, an NSP or installed on the
                                  drive, cropped to its bounding box and prefixed by u16 x, y, width,
                                  height (little endian) in 256x256 icon space.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usbl_shapes as S

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'stratosphere', 'ams_mitm', 'source', 'usbl', 'assets')

def write(name, data):
    path = os.path.join(OUT_DIR, name)
    with open(path, 'wb') as f:
        f.write(data)
    print(f'{name}: {len(data)} bytes')

def crop(rgba, width, height):
    xs = [i % width for i in range(width * height) if rgba[i * 4 + 3] != 0]
    ys = [i // width for i in range(width * height) if rgba[i * 4 + 3] != 0]
    x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
    rows = [rgba[(y * width + x0) * 4:(y * width + x1) * 4] for y in range(y0, y1)]
    return x0, y0, x1 - x0, y1 - y0, b''.join(rows)

def main():
    for name, state in (('connected', 'primary'), ('removed', 'pending'), ('unreadable', 'error')):
        write(f'usbloadernx-{name}.rgba', S.rasterize(S.notif_icon(state), 50, 50))

    badges = (
        ('xci',       S.badge_usb_source(S.gamecard_silhouette, S.gamecard_details)),
        ('nsp',       S.badge_usb_source(S.package_glyph, S.package_details)),
        ('installed', S.badge_usb_source(S.blocks_glyph)),
    )
    for source, scene in badges:
        x, y, w, h, pixels = crop(S.rasterize(scene, 256, 256), 256, 256)
        write(f'usbloadernx-badge-{source}.rgba', struct.pack('<4H', x, y, w, h) + pixels)

if __name__ == '__main__':
    main()
