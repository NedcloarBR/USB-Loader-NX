#!/usr/bin/env python3
"""Regenerates the Manager's sound effects in manager/romfs/sfx/ (16-bit mono WAV, 48 kHz).

Every sound is synthesized here (soft sines with bell-like overtones and short envelopes), so the Manager ships no
third-party audio."""
import math
import os
import random
import struct
import wave

RATE = 48000
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'manager', 'romfs', 'sfx')

def note(name):
    """'E6' -> Hz."""
    names = {'C': -9, 'D': -7, 'E': -5, 'F': -4, 'G': -2, 'A': 0, 'B': 2}
    semitone = names[name[0]] + (1 if '#' in name else 0)
    octave = int(name[-1])
    return 440.0 * 2 ** ((semitone + (octave - 4) * 12) / 12)

def silence(seconds):
    return [0.0] * int(RATE * seconds)

def bell(freq, seconds, decay, gain=1.0, attack=0.012):
    """A sine with two faint overtones, a soft attack and an exponential decay."""
    out = []
    for i in range(int(RATE * seconds)):
        t = i / RATE
        env = min(1.0, t / attack) * math.exp(-t * decay)
        s = math.sin(2 * math.pi * freq * t) + 0.12 * math.sin(2 * math.pi * freq * 2 * t) * math.exp(-t * decay * 1.5) \
            + 0.03 * math.sin(2 * math.pi * freq * 3.01 * t) * math.exp(-t * decay * 2.5)
        out.append(s * env * gain)
    return out

def mix(*tracks):
    """Each track is (start seconds, samples)."""
    length = max(int(start * RATE) + len(samples) for start, samples in tracks)
    out = [0.0] * length
    for start, samples in tracks:
        offset = int(start * RATE)
        for i, s in enumerate(samples):
            out[offset + i] += s
    return out

def whoosh(seconds, low, high, gain):
    """Noise through a resonant band-pass sweeping from `low` to `high` Hz."""
    rng = random.Random(7)
    out, y1, y2 = [], 0.0, 0.0
    n = int(RATE * seconds)
    for i in range(n):
        p = i / n
        freq = low * (high / low) ** p
        r = 0.985
        a1, a2 = -2 * r * math.cos(2 * math.pi * freq / RATE), r * r
        x = rng.uniform(-1, 1)
        y = x * (1 - r) - a1 * y1 - a2 * y2
        y2, y1 = y1, y
        out.append(y * math.sin(math.pi * p) ** 1.5 * gain)
    return out

def write(name, samples, peak=0.55):
    top = max(abs(s) for s in samples) or 1.0
    # A short fade out keeps the end click-free.
    fade = int(RATE * 0.01)
    data = bytearray()
    for i, s in enumerate(samples):
        k = min(1.0, (len(samples) - i) / fade)
        data += struct.pack('<h', int(s / top * peak * k * 32767))
    path = os.path.join(OUT_DIR, name + '.wav')
    with wave.open(path, 'wb') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes(bytes(data))
    print(f'{name}.wav: {len(data)} bytes')

def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    write('move', bell(note('E6'), 0.08, 45, attack=0.006), peak=0.10)
    write('select', mix((0, bell(note('E5'), 0.3, 14)), (0.06, bell(note('B5'), 0.36, 11))), peak=0.2)
    write('back', mix((0, bell(note('B4'), 0.26, 15)), (0.06, bell(note('E4'), 0.32, 13))), peak=0.18)
    write('whoosh', whoosh(0.24, 300, 1400, 1.0), peak=0.12)
    write('toggle_on', mix((0, bell(note('G5'), 0.16, 22)), (0.05, bell(note('D6'), 0.2, 19))), peak=0.16)
    write('toggle_off', mix((0, bell(note('D6'), 0.16, 22)), (0.05, bell(note('G5'), 0.2, 19))), peak=0.15)
    write('launch', mix(*[(i * 0.08, bell(note(n), 0.8 - i * 0.08, 5, 1.0 - i * 0.12, attack=0.02)) for i, n in enumerate(('C5', 'E5', 'G5', 'C6', 'E6'))],
                        (0.0, whoosh(0.5, 250, 2400, 0.35))), peak=0.26)
    write('error', mix((0, bell(note('A3'), 0.22, 13)), (0.14, bell(note('F3'), 0.3, 10))), peak=0.22)
    write('plug', mix((0, bell(note('C5'), 0.24, 13)), (0.07, bell(note('G5'), 0.26, 11)), (0.14, bell(note('C6'), 0.34, 9))), peak=0.2)
    write('unplug', mix((0, bell(note('C6'), 0.22, 14)), (0.07, bell(note('G5'), 0.24, 12)), (0.14, bell(note('C5'), 0.32, 10))), peak=0.18)
    write('open', mix((0, whoosh(0.2, 400, 1300, 0.4)), (0.04, bell(note('A5'), 0.26, 15, 0.7))), peak=0.14)

if __name__ == '__main__':
    main()
