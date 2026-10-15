"""
The cel-shaded pass of the blended style. A Link to the Past's dungeon art is
shaded with dithering and many close tones; Minish Cap's looks like an animated
cartoon: flat bands of color, bold dark outlines where shapes meet, a bright rim
where light catches an edge, and soft shadows cast across the floor. This pass
turns one into the other a whole room at a time, so outlines and shadows follow
shapes across tile edges.

A room is a 512x512 RGB array plus a mask of empty pixels. Light comes from the
upper left, as in Minish Cap.
"""
import colorsys

import numpy as np

OUTLINE = np.array([32, 24, 0], dtype=float)

# soft shadows: how far they reach (pixels) and how dark each band is, nearest first
SHADOW_REACH = 12
SHADOW_BANDS = [(4, 0.66), (8, 0.78), (12, 0.89)]
SHADOW_TINT = np.array([0.94, 0.96, 1.08])   # shadows lean cool, the lit floor warm


def lightness(rgb):
    mx = rgb.max(axis=-1)
    mn = rgb.min(axis=-1)
    return (mx + mn) / 510.0


def snap(rgb):
    """To the Game Boy Advance's 5-bit channels."""
    return np.clip(np.round(rgb / 8.0) * 8, 0, 248)


def flatten_tones(colors):
    """Merges close tones into flat bands and pushes the bands apart. Returns
    {color: new color} for a list of RGB tuples."""
    hls = {c: colorsys.rgb_to_hls(*[v / 255 for v in c]) for c in colors}
    groups = []
    for c in sorted(colors, key=lambda c: hls[c][1]):
        h, l, s = hls[c]
        for g in groups:
            gh, gl, gs = hls[g[0]]
            dh = min(abs(h - gh), 1 - abs(h - gh))
            if abs(l - gl) < 0.09 and (dh < 0.07 or (s < 0.12 and gs < 0.12)):
                g.append(c)
                break
        else:
            groups.append([c])
    out = {}
    for g in groups:
        h, l, s = hls[g[0]]
        if l < 0.13:
            new = tuple(OUTLINE)
        elif l < 0.45:
            # shadow tones: a little deeper and cooler
            r, gg, b = colorsys.hls_to_rgb((h + (0.015 if 0.2 < h < 0.6 else -0.01)) % 1, l * 0.9, min(1, s * 1.12))
            new = (r * 255, gg * 255, b * 255)
        else:
            # lit tones: brighter and warmer
            r, gg, b = colorsys.hls_to_rgb(h, min(0.95, l + (0.95 - l) * 0.2), min(1, s * 1.05))
            new = (r * 255, gg * 255, b * 255)
        new = tuple(int(v) for v in snap(np.array(new)))
        for c in g:
            out[c] = new
    return out


def _shift(a, dx, dy, fill):
    """a moved by (dx, dy): out[y][x] = a[y - dy][x - dx]."""
    out = np.full_like(a, fill)
    h, w = a.shape[:2]
    ys, yd = (slice(0, h - dy), slice(dy, h)) if dy >= 0 else (slice(-dy, h), slice(0, h + dy))
    xs, xd = (slice(0, w - dx), slice(dx, w)) if dx >= 0 else (slice(-dx, w), slice(0, w + dx))
    out[yd, xd] = a[ys, xs]
    return out


def shadows(img, receive, cast):
    """Soft shadows on the floor (receive) from walls and objects (cast), falling
    down and to the right. Three bands, darkest nearest the caster."""
    dist = np.full(receive.shape, 99, dtype=int)
    for t in range(SHADOW_REACH, 0, -1):
        for dx, dy in ((0, t), (t, 0), (t, t), (t // 2, t)):
            hit = _shift(cast, dx, dy, False)
            dist[hit] = t
    out = img.copy()
    for reach, k in reversed(SHADOW_BANDS):
        m = receive & (dist <= reach)
        out[m] = img[m] * k * SHADOW_TINT
    return snap(out)


def shade(img, empty, receive, cast, soft=None):
    """img: HxWx3 RGB floats (already flattened into bands), empty: pixels with no
    color. receive/cast: where shadows fall and what casts them. soft: pixels that get
    no outlines or rim light (fine studded detail they would turn busy)."""
    h, w = empty.shape
    img = img.copy()

    # despeckle: a lone pixel inside a flat area takes the area's color
    nbrs = [_shift(img, dx, dy, -1) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))]
    same = [np.all(n == img, axis=-1) for n in nbrs]
    lone = ~(same[0] | same[1] | same[2] | same[3]) & ~empty
    agree = np.zeros(empty.shape, dtype=int)
    pick = np.zeros_like(img)
    for i in range(4):
        cnt = sum(np.all(nbrs[i] == nbrs[j], axis=-1).astype(int) for j in range(4))
        better = cnt > agree
        agree[better] = cnt[better]
        pick[better] = nbrs[i][better]
    fix = lone & (agree >= 3) & (pick[..., 0] >= 0)
    img[fix] = pick[fix]

    img = shadows(img, receive & ~empty, cast)

    # outlines: the darker side of a strong edge between shapes
    lit = lightness(img)
    lit[empty] = 0
    edge = np.zeros(empty.shape, dtype=bool)
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        other = _shift(lit, dx, dy, 0)
        edge |= (other - lit > 0.28)
    edge &= ~empty & (lit > 0.13)
    if soft is not None:
        edge &= ~soft
    img[edge] = OUTLINE

    # rim light where a lit surface meets an outline above or to its left
    lit = lightness(img)
    is_line = np.all(img == OUTLINE, axis=-1)
    rim = (_shift(is_line, 0, 1, False) | _shift(is_line, 1, 0, False)) & ~is_line & ~empty & (lit > 0.4)
    if soft is not None:
        rim &= ~soft
    img[rim] = snap(img[rim] + (255 - img[rim]) * 0.45)
    return img
