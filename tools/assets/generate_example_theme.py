#!/usr/bin/env python3
"""Regenerates the assets of the example theme manager/themes/midnight/: background.jpg (a night sky, 1920x1080),
thumb.jpg (the theme picker's preview, 320x180: the sky with the shelf drawn in the theme's colors) and music.wav (a slow, quiet pad that loops). Everything is synthesized here; the JPEG goes through rgb_to_jpg.c."""
import math
import os
import random
import struct
import subprocess
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, '..', '..', 'manager', 'themes', 'midnight')
WIDTH, HEIGHT = 1920, 1080
RATE = 22050

def background():
    top, bottom = (0x0B, 0x10, 0x20), (0x1A, 0x22, 0x40)
    rows = []
    for y in range(HEIGHT):
        t = y / (HEIGHT - 1)
        rows.append(bytes(int(top[c] + (bottom[c] - top[c]) * t) for c in range(3)) * WIDTH)
    pixels = bytearray(b''.join(rows))
    rng = random.Random(42)
    for _ in range(900):
        x, y = rng.randrange(WIDTH), rng.randrange(int(HEIGHT * 0.8))
        level = rng.randrange(70, 200)
        for dx, dy, k in ((0, 0, 1.0), (1, 0, 0.35), (-1, 0, 0.35), (0, 1, 0.35), (0, -1, 0.35)):
            px, py = x + dx, y + dy
            if 0 <= px < WIDTH and 0 <= py < HEIGHT:
                o = (py * WIDTH + px) * 3
                for c, tint in enumerate((0.9, 0.92, 1.0)):
                    pixels[o + c] = min(255, pixels[o + c] + int(level * k * tint))
    return bytes(pixels)

THUMB_W, THUMB_H = 320, 180

def thumbnail(sky):
    """The sky scaled down 6x (box filter), with the shelf in the theme's colors: a large cover ringed in Primary,
    smaller covers queued after it, a title line and a button."""
    k = WIDTH // THUMB_W
    pixels = bytearray(THUMB_W * THUMB_H * 3)
    for ty in range(THUMB_H):
        for tx in range(THUMB_W):
            acc = [0, 0, 0]
            for y in range(ty * k, ty * k + k, 2):
                row = y * WIDTH * 3
                for x in range(tx * k, tx * k + k, 2):
                    i = row + x * 3
                    acc[0] += sky[i]; acc[1] += sky[i + 1]; acc[2] += sky[i + 2]
            n = (k // 2) * (k // 2)
            o = (ty * THUMB_W + tx) * 3
            pixels[o:o + 3] = bytes(v // n for v in acc)

    def rect(x, y, w, h, color, r=0):
        for py in range(y, y + h):
            for px in range(x, min(x + w, THUMB_W)):
                dx = max(x + r - px - 0.5, px + 0.5 - (x + w - r), 0)
                dy = max(y + r - py - 0.5, py + 0.5 - (y + h - r), 0)
                if r and dx * dx + dy * dy > r * r:
                    continue
                o = (py * THUMB_W + px) * 3
                pixels[o:o + 3] = bytes(color)

    primary, raised, panel, text, muted = (0x7C, 0x8C, 0xFF), (0x22, 0x2C, 0x48), (0x18, 0x20, 0x36), (0xEE, 0xF1, 0xFA), (0x98, 0xA2, 0xC4)
    rect(10, 12, 9, 9, primary, 2)
    rect(24, 14, 60, 5, text, 2)
    rect(9, 27, 74, 74, primary, 10)
    rect(12, 30, 68, 68, raised, 8)
    for i in range(5):
        rect(92 + i * 54, 51, 48, 48, panel, 6)
    rect(0, 104, THUMB_W, 1, panel)
    rect(12, 118, 120, 9, text, 3)
    rect(12, 134, 70, 6, muted, 3)
    rect(236, 144, 72, 18, primary, 9)
    return bytes(pixels)

def music(seconds=24.0):
    """Four slow chords (sine pads with a soft attack and release); the end fades into the start so it loops."""
    chords = [(220.0, 277.18, 329.63), (196.0, 246.94, 293.66), (174.61, 220.0, 261.63), (196.0, 246.94, 329.63)]
    length = int(RATE * seconds)
    per_chord = length // len(chords)
    samples = []
    for i in range(length):
        chord = chords[min(i // per_chord, len(chords) - 1)]
        local = (i % per_chord) / per_chord
        envelope = math.sin(math.pi * local) ** 0.8
        t = i / RATE
        value = sum(math.sin(2 * math.pi * f * t) + 0.3 * math.sin(2 * math.pi * f * 2 * t + 0.5) for f in chord)
        samples.append(value * envelope * (0.85 + 0.15 * math.sin(2 * math.pi * 0.2 * t)))
    peak = max(abs(s) for s in samples) or 1.0
    return b''.join(struct.pack('<h', int(s / peak * 0.45 * 32767)) for s in samples)

def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        tool = os.path.join(tmp, 'rgb_to_jpg')
        subprocess.run(['cc', '-O2', '-o', tool, os.path.join(HERE, 'rgb_to_jpg.c'), '-lm'], check=True)
        out = os.path.join(OUT_DIR, 'background.jpg')
        sky = background()
        subprocess.run([tool, str(WIDTH), str(HEIGHT), '88', out], input=sky, check=True)
        print(f'background.jpg: {os.path.getsize(out)} bytes')
        thumb = os.path.join(OUT_DIR, 'thumb.jpg')
        subprocess.run([tool, str(THUMB_W), str(THUMB_H), '90', thumb], input=thumbnail(sky), check=True)
        print(f'thumb.jpg: {os.path.getsize(thumb)} bytes')

    out = os.path.join(OUT_DIR, 'music.wav')
    with wave.open(out, 'wb') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes(music())
    print(f'music.wav: {os.path.getsize(out)} bytes')

if __name__ == '__main__':
    main()
