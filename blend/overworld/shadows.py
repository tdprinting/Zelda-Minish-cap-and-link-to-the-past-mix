"""
Soft shadows for the overworld, drawn on a background layer of their own.

The Game Boy Advance can blend one background over the others. The blend uses
background 3 for a shadow layer: wherever it has a pixel, the ground (and Link,
and anything else below it) is darkened and cooled a little, so shadows look
soft and see-through instead of painted onto the tiles. The layer sits under
the top layer, so tree tops and roofs stay bright.

For every room this module works out where shadows fall:
  * casters are everything solid that is not ground (trees, cliffs, walls,
    buildings, fences, signs, rocks); taller (thicker) shapes reach further;
  * light comes from the upper left, as in Minish Cap, so shadows fall down
    and to the right, darkest at the caster's foot and fading out with distance;
  * the darkness is sampled at the corners of the 8x8 cells and quantized to
    four levels; a cell's character is the bilinear blend of its four corners,
    drawn with three shades of shadow and an ordered dither, so neighbouring
    cells always meet without seams. The most common corner patterns (and their
    mirror images) become the characters; rare ones use the nearest stored one.

Output per area: the shadow characters (placed in character slots the area's
tileset never uses), three shadow colors (in unused palette slots), and per
room one byte per 8x8 cell naming its character.
"""
import numpy as np

import owdata
from restyle import _shift, _thickness

Q = 4                      # darkness levels at the corners: 0 (lit) .. 3
REACH = {1: 6, 2: 11, 3: 16}
FALLOFF = 0.8

# the hardware blend: shown = (EVA * shadow color + EVB * what is below) / 16
EVA, EVB = 5, 11
# how dark the lightest and the deepest shade leave typical ground
LIGHTEST, DEEPEST = 0.86, 0.69
MAX_SHADES = 5
TINT = np.array([0.88, 0.97, 1.18])     # shadows lean cool (sky light)


def darkness(plan, ts, w, h, b, t, ground):
    """Per pixel: how deep in shadow (0..1), plus the layers for previews."""
    eb, cb, et, ct, receive, solid, wet = plan.room_layers(ts, w, h, b, t)
    up = np.ones((16, 16), dtype=bool)
    pal = plan.palette_rgb(ts)
    rgb = pal[eb >> 12, cb]
    v = (rgb[..., 0] >> 3) | ((rgb[..., 1] >> 3) << 5) | ((rgb[..., 2] >> 3) << 10)
    is_ground = np.isin(v, list(ground)) & (cb != 0)
    top = ct != 0
    thick = _thickness(solid)
    cast = np.kron(solid, up) & ~is_ground & (cb != 0)
    cast |= top & ~np.kron(receive, up)
    reach = np.kron(np.where(solid, thick, 0), up)
    reach[top & (reach == 0)] = 3
    D = np.zeros(cast.shape)
    for k, r in REACH.items():
        src = cast & (reach == k) if k < 3 else cast & (reach >= 3)
        if not src.any():
            continue
        for d in range(1, r + 1):
            val = (1 - (d - 1) / r) ** FALLOFF
            for dx, dy in ((0, d), (d, 0), (d, d), (d // 2, d), (d, d // 2)):
                hit = _shift(src, dx, dy, False)
                D[hit] = np.maximum(D[hit], val)
    acc = np.zeros_like(D)
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            acc += _shift(D, dx, dy, 0)
    D = acc / 9
    D[cast & ~top] = 0          # solid things are not shaded by their own shadow
    return D, (eb, cb, et, ct)


def corners(D):
    """Darkness levels 0..Q-1 at each cell corner (averaged over 8x8 around it)."""
    H, W = D.shape
    pad = np.pad(D, ((4, 4), (4, 4)))
    c = np.zeros((H // 8 + 1, W // 8 + 1))
    # sum over 8x8 windows centred on the corners via a summed-area table
    sat = np.zeros((H + 9, W + 9))
    sat[1:, 1:] = np.cumsum(np.cumsum(pad, axis=0), axis=1)
    ys = np.arange(H // 8 + 1) * 8
    xs = np.arange(W // 8 + 1) * 8
    Y, X = np.meshgrid(ys, xs, indexing='ij')
    c = (sat[Y + 8, X + 8] - sat[Y, X + 8] - sat[Y + 8, X] + sat[Y, X]) / 64
    return np.clip(np.round(c * (Q - 1)), 0, Q - 1).astype(np.int64)


def cell_pattern(k, shades=3):
    """8x8 shade levels (0 = none .. shades) for corner levels (tl, tr, bl, br):
    the bilinear blend of the corners in flat bands."""
    tl, tr, bl, br = [v / (Q - 1) for v in k]
    y, x = np.mgrid[0:8, 0:8]
    fy, fx = (y + 0.5) / 8, (x + 0.5) / 8
    v = (tl * (1 - fx) * (1 - fy) + tr * fx * (1 - fy) + bl * (1 - fx) * fy + br * fx * fy) * shades
    return np.clip(np.floor(v + 0.5), 0, shades).astype(np.uint8)


def flips(k):
    tl, tr, bl, br = k
    return {(0, 0): k, (1, 0): (tr, tl, br, bl), (0, 1): (bl, br, tl, tr), (1, 1): (br, bl, tr, tl)}


class ShadowArea:
    """Shadow layer data for one area (all of its tilesets)."""

    def __init__(self, plan, budget=None, log=print):
        self.plan = plan
        self.log = log
        self.rooms = {}          # room -> (ts index, w, h, corner levels)
        self.ground = {}
        counts = {}
        for ts in plan.tilesets:
            if not ts.rooms:
                continue
            g = self.ground[ts.ts_index] = plan.ground_keys(ts)
            for i, w, h, b, t in ts.rooms:
                D, _ = darkness(plan, ts, w, h, b, t, g)
                C = corners(D)
                self.rooms[i] = (ts.ts_index, w, h, C)
                k = np.stack([C[:-1, :-1], C[:-1, 1:], C[1:, :-1], C[1:, 1:]], axis=-1).reshape(-1, 4)
                for key in map(tuple, k[k.any(axis=1)]):
                    counts[key] = counts.get(key, 0) + 1
        self.counts = counts
        self.choose(budget)

    def choose(self, budget):
        """Picks the stored corner patterns: most common first, each with its
        mirror images for free."""
        budget = budget if budget is not None else 255
        stored = {}              # canonical key -> char number (1..)
        lookup = {}              # any key -> (char number, hflip, vflip)
        for key in sorted(self.counts, key=lambda k: -self.counts[k]):
            if key in lookup:
                continue
            if len(stored) >= budget:
                break
            n = len(stored) + 1
            stored[key] = n
            for (hf, vf), k2 in flips(key).items():
                lookup.setdefault(k2, (n, hf, vf))
        self.stored = stored
        self.lookup = lookup
        missing = [k for k in self.counts if k not in lookup]
        for k in missing:
            best, bd = None, 1e9
            for k2 in lookup:
                d = sum(abs(a - c) for a, c in zip(k, k2))
                if d < bd:
                    bd, best = d, k2
            lookup[k] = lookup[best]
        total = sum(self.counts.values())
        miss = sum(self.counts[k] for k in missing)
        self.log('  %s: %d corner patterns, %d characters stored, %.1f%% of shaded cells approximated'
                 % (self.plan.label, len(self.counts), len(stored), 100.0 * miss / max(1, total)))

    def chars(self, shades=3):
        """{char number: 8x8 shade levels}"""
        return {n: cell_pattern(k, shades) for k, n in self.stored.items()}

    def cell_map(self, room):
        """Per cell: (char number, hflip, vflip), char number 0 = no shadow."""
        tsi, w, h, C = self.rooms[room]
        out = np.zeros((h * 2, w * 2, 3), dtype=np.int64)
        for cy in range(h * 2):
            for cx in range(w * 2):
                k = (int(C[cy, cx]), int(C[cy, cx + 1]), int(C[cy + 1, cx]), int(C[cy + 1, cx + 1]))
                if any(k):
                    out[cy, cx] = self.lookup[k]
        return out

    def levels(self, room, shades=3):
        """The shadow layer's shade per pixel, as the game will draw it."""
        cm = self.cell_map(room)
        ch = self.chars(shades)
        h2, w2 = cm.shape[:2]
        L = np.zeros((h2 * 8, w2 * 8), dtype=np.uint8)
        for cy in range(h2):
            for cx in range(w2):
                n, hf, vf = cm[cy, cx]
                if n:
                    p = ch[n]
                    if hf:
                        p = p[:, ::-1]
                    if vf:
                        p = p[::-1]
                    L[cy * 8:cy * 8 + 8, cx * 8:cx * 8 + 8] = p
        return L


def shades(n):
    """How dark each of n shades leaves typical ground, lightest first."""
    if n == 1:
        return [(LIGHTEST + DEEPEST) / 2]
    return [LIGHTEST + (DEEPEST - LIGHTEST) * i / (n - 1) for i in range(n)]


def shadow_colors(ground_rgb, n=3):
    """The n shadow colors (lightest first) for an area whose plain ground
    is mostly ground_rgb, so that blending leaves that ground at shades(n)."""
    g = np.array(ground_rgb, dtype=float)
    lum = g.mean()
    base = g * 0.5 + lum * 0.5          # less saturated: shadows mute colors
    out = []
    for f in shades(n):
        k = max(0.0, (f - EVB / 16) / (EVA / 16))
        c = np.clip(base * k * TINT, 0, 248)
        out.append(tuple(int(round(v / 8)) * 8 for v in c))
    return out


def composite(img, levels, colors, top_mask):
    """What the hardware blend shows (top layer stays unblended)."""
    out = img.astype(float).copy()
    for lvl in range(1, len(colors) + 1):
        m = (levels == lvl) & ~top_mask
        s = np.array(colors[lvl - 1], dtype=float)
        out[m] = (EVA * s + EVB * out[m]) / 16
    return np.clip(np.floor(out / 8) * 8, 0, 248).astype(np.uint8)
