"""
The blended overworld colors: Minish Cap's overworld palettes moved toward A Link
to the Past's Light World hues (its deeper, cooler grass, tan earth and teal
water), then pushed into the cel-shaded look: flat bands set further apart,
warm sunlit tops, cool shadows, and warm brown-black outlines.

The A Link to the Past colors come from the user's own ROM at build time.
"""
import colorsys
import os
import struct

import numpy as np

OUTLINE = (32, 24, 0)

# where A Link to the Past keeps its overworld background colors (SNES addresses)
ALTTP_MAIN = (0x9BE6C8, 210)     # 6 modes x 35
ALTTP_AUX12 = (0x9BE86C, 420)    # 20 sets x 21


def _snes_to_pc(rom, addr):
    return len(rom) % 0x8000 + ((addr >> 16) & 0x7f) * 0x8000 + (addr & 0x7fff)


def alttp_light_world(rom_path):
    """The Light World's overworld colors from an A Link to the Past (USA) ROM, as RGB."""
    rom = open(rom_path, 'rb').read()
    out = []
    for addr, n in (ALTTP_MAIN, ALTTP_AUX12):
        p = _snes_to_pc(rom, addr)
        words = struct.unpack('<%dH' % n, rom[p:p + 2 * n])
        if addr == ALTTP_MAIN[0]:
            words = words[:35]              # mode 0 is the Light World
        else:
            words = words[:21 * 10]
        out += [((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3) for c in words]
    return sorted(set(out))


def save_reference(colors, path):
    with open(path, 'wb') as f:
        for r, g, b in colors:
            f.write(bytes((r, g, b)))


def load_reference(path):
    d = open(path, 'rb').read()
    return [tuple(d[i:i + 3]) for i in range(0, len(d) - 2, 3)]


def snap(v):
    return min(248, max(0, int(round(v / 8.0)) * 8))


def _hls(rgb):
    return colorsys.rgb_to_hls(*[c / 255 for c in rgb])


def _hue_dist(a, b):
    d = abs(a - b) % 1.0
    return min(d, 1 - d)


# Minish Cap hue ranges and the A Link to the Past hues they move toward:
# (MC range, ALttP range, hue pull, saturation pull, lightness scale)
FAMILIES = [
    ('green', (0.17, 0.47), (0.24, 0.46), 0.55, 0.45, 0.93),
    ('earth', (0.02, 0.17), (0.04, 0.17), 0.40, 0.20, 0.99),
    ('water', (0.47, 0.80), (0.47, 0.70), 0.45, 0.35, 0.97),
]


class Style:
    """Turns one Minish Cap overworld color into its blended, cel-shaded version.
    strength scales every pull toward A Link to the Past (0 = Minish Cap hues)."""

    def __init__(self, reference, strength=1.0):
        self.ref = [(_hls(c), c) for c in reference]
        self.strength = strength

    def family(self, h, s):
        if s < 0.1:
            return None
        for f in FAMILIES:
            lo, hi = f[1]
            if lo <= h < hi:
                return f
        return None

    def target(self, fam, l):
        lo, hi = fam[2]
        best, bd = None, 9
        for (rh, rl, rs), c in self.ref:
            if rs < 0.15 or not (lo <= rh < hi):
                continue
            d = abs(l - rl) + 0.3 * abs(rh - (lo + hi) / 2)
            if d < bd:
                bd, best = d, (rh, rl, rs)
        return best

    def color(self, rgb):
        h, l, s = _hls(rgb)
        if l < 0.13 and s < 0.6 or l < 0.08:
            return OUTLINE
        fam = self.family(h, s)
        if fam is not None:
            t = self.target(fam, l)
            if t is not None:
                k = self.strength
                dh = ((t[0] - h + 0.5) % 1.0) - 0.5
                h = (h + dh * fam[3] * k) % 1.0
                s = s + (t[2] - s) * fam[4] * k
                l = l * (1 - (1 - fam[5]) * k)
        # cel push: lights up and warm, shadows down and cool, bands further apart
        mid = 0.5
        if l >= mid:
            l2 = l + (0.96 - l) * 0.10
            dh = ((0.12 - h + 0.5) % 1.0) - 0.5
            h = (h + dh * 0.08 * (l - mid) / 0.5) % 1.0
        else:
            l2 = l - (l - 0.10) * 0.12
            dh = ((0.62 - h + 0.5) % 1.0) - 0.5
            h = (h + dh * 0.06 * (mid - l) / mid) % 1.0
            s = min(1.0, s * 1.05)
        r, g, b = colorsys.hls_to_rgb(h, min(0.97, max(0, l2)), min(1, max(0, s)))
        return (snap(r * 255), snap(g * 255), snap(b * 255))


def calm(colors, weights=None):
    """Merges near-identical tones (texture speckle) into flat bands.
    Returns {color: representative}."""
    weights = weights or {}
    hls = {c: _hls(c) for c in colors}
    order = sorted(colors, key=lambda c: -weights.get(c, 0))
    reps = []
    out = {}
    for c in order:
        h, l, s = hls[c]
        for r in reps:
            rh, rl, rs = hls[r]
            if abs(l - rl) < 0.035 and (_hue_dist(h, rh) < 0.03 or (s < 0.1 and rs < 0.1)) and abs(s - rs) < 0.15:
                out[c] = r
                break
        else:
            reps.append(c)
            out[c] = c
    return out
