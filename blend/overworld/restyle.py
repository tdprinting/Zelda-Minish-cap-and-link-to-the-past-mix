"""
One Minish Cap overworld area, prepared for the blend's look: its palettes
recolored (owstyle) and its rooms read for where the ground is and what stands
on it, which shadows.py uses to work out the soft shadows.

Nothing here changes tile graphics, metatiles, tile types or room maps, so
collision and every tile action stay exactly as Minish Cap has them, and the
animated tiles (water, flowers) keep animating in the new colors.
"""
import os
import sys

import numpy as np

import owdata
import owstyle
from owdata import FILLER_TYPE, TOP_BASE

sys.path.insert(0, os.path.join(owdata.HERE, '..', 'dungeons'))
import mctiles  # noqa: E402

COLLISION = np.array(mctiles.COLLISION + [0] * (4096 - len(mctiles.COLLISION)))
ACT = np.array(mctiles.ACT + [0] * (4096 - len(mctiles.ACT)))

# plain ground: walkable, no special action (grass, earth, sand, stone paths)
RECEIVE_ACTS = (9, 10, 11, 12)
# solid tiles that cast no shadow: deep and shallow water
NO_CAST_ACTS = (15, 16)
ACT_PIT = 0x19


def _shift(a, dx, dy, fill):
    """a moved by (dx, dy): out[y][x] = a[y - dy][x - dx]."""
    out = np.full_like(a, fill)
    h, w = a.shape[:2]
    ys, yd = (slice(0, h - dy), slice(dy, h)) if dy >= 0 else (slice(-dy, h), slice(0, h + dy))
    xs, xd = (slice(0, w - dx), slice(dx, w)) if dx >= 0 else (slice(-dx, w), slice(0, w + dx))
    out[yd, xd] = a[ys, xs]
    return out


def _thickness(mask):
    """Per cell: how many cells thick the solid shape is there (1, 2 or 3+)."""
    t = np.zeros(mask.shape, dtype=np.int64)
    cur = mask.copy()
    for k in (1, 2, 3):
        t[cur] = k
        er = cur.copy()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            er &= _shift(cur, dx, dy, False)
        grown = er.copy()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            grown |= _shift(er, dx, dy, False)
        cur = grown & mask & cur
        if not cur.any():
            break
    return t


class TileSetPlan:
    """One character set + palette group of an area, and the rooms drawn with it."""

    def __init__(self, index, ts_index):
        self.area = owdata.Area(index, ts_index)
        self.ts_index = ts_index
        self.parts = self.area.parts
        self.chars = self.area.chars
        self.rows = {r: list(cols) for r, (pid, cols) in self.area.rows.items()}
        self.pids = {r: pid for r, (pid, cols) in self.area.rows.items()}
        self.rooms = list(self.area.rooms())


class AreaPlan:
    def __init__(self, index, style, log=print):
        self.index = index
        self.log = log
        self.style = style
        tables = owdata.area_tables()
        self.tables = tables[index]
        base = owdata.Area(index, 0)
        self.label = base.label
        self.meta = base.meta
        self.types = base.types
        ts_count = len(owdata.tileset_list(self.tables['tilesets']))
        self.tilesets = [TileSetPlan(index, i) for i in range(ts_count)]
        # tile animations of every area drawn with this area's tilesets
        self.anims = []
        for i, t in enumerate(tables):
            if t['tilesets'] == self.tables['tilesets']:
                self.anims += owdata.area_animations(i)
        self.usage = {}

    # ------------------------------------------------------------ what is in use

    def anim_chars(self):
        out = set()
        for aid, frames in self.anims:
            for fr in frames:
                for vram, size, is_pal, sym in fr:
                    if not is_pal:
                        out |= set(range(vram // 32, vram // 32 + size))
        return out

    def referenced_chars(self):
        ref = set()
        for e in self.meta['bottom']:
            ref.add(int(e) & 0x3ff)
        for e in self.meta['top']:
            ref.add((int(e) & 0x3ff) + TOP_BASE)
        return ref

    def free_top_chars(self, ts):
        """Character slots in block 2 (0x6008000..) that no metatile or animation uses."""
        ref = self.referenced_chars() | self.anim_chars()
        out = []
        for c in range(1024, 1536):
            if c in ref:
                continue
            if not any(dest <= 0x6000000 + c * 32 < dest + size for _, dest, size in ts.parts):
                continue
            out.append(c)
        return out

    def color_usage(self, ts):
        """{(row, color index): pixels} over every character a live metatile uses,
        with the animation frames drawn at their characters."""
        use = {}
        anim_frames = {}
        for aid, frames in self.anims:
            for fr in frames:
                for vram, size, is_pal, sym in fr:
                    if is_pal:
                        continue
                    data = owdata.decode_chars(open(owdata.asset_path(sym), 'rb').read()[:size * 32])
                    for k in range(size):
                        anim_frames.setdefault(vram // 32 + k, []).append(data[k])
        for layer, base in (('bottom', 0), ('top', TOP_BASE)):
            meta = self.meta[layer]
            types = self.types[layer]
            live = [i for i in range(len(meta) // 4) if i >= len(types) or types[i] != FILLER_TYPE]
            ent = np.unique(meta.reshape(-1, 4)[live]) if live else np.array([], dtype=np.int64)
            for e in ent:
                e = int(e)
                c = (e & 0x3ff) + base
                row = e >> 12
                for p in [ts.chars[c]] + anim_frames.get(c, []):
                    vals, counts = np.unique(p, return_counts=True)
                    for v, n in zip(vals, counts):
                        use[(row, int(v))] = use.get((row, int(v)), 0) + int(n)
        return use

    def free_palette_slots(self, ts):
        """{row: [color indices no live tile uses]}"""
        u = self.usage[ts.ts_index]
        return {r: [i for i in range(1, 16) if (r, i) not in u] for r in sorted(ts.rows)}

    # ------------------------------------------------------------ palettes

    def restyle_palettes(self, ts):
        """Recolors every palette row; near-identical tones merge into one flat band."""
        usage = self.usage[ts.ts_index]
        weights = {}
        for (row, idx), n in usage.items():
            if row in ts.rows and idx:
                c = owdata.gba_to_rgb(ts.rows[row][idx])
                weights[c] = weights.get(c, 0) + n
        merge = owstyle.calm(list(weights), weights)
        for row, cols in ts.rows.items():
            new = []
            for idx, c in enumerate(cols):
                rgb = owdata.gba_to_rgb(c)
                if idx and (row, idx) in usage:
                    rgb = merge.get(rgb, rgb)
                new.append(owdata.rgb_to_gba(self.style.color(rgb)) if idx else c)
            ts.rows[row] = new

    def run(self):
        for ts in self.tilesets:
            self.usage[ts.ts_index] = self.color_usage(ts)
            self.restyle_palettes(ts)

    # ------------------------------------------------------------ rooms

    def room_layers(self, ts, w, h, b, t):
        """Per pixel: bottom entry and color index, top entry and color index;
        per cell: plain ground, solid, and water or pit."""
        eb, cb = owdata.draw_layer(ts.area, b, w, h, 'bottom', ts.chars, self.meta['bottom'])
        et, ct = owdata.draw_layer(ts.area, t, w, h, 'top', ts.chars, self.meta['top'])
        bb = np.asarray(b).reshape(h, w)
        tb = self.types['bottom']
        ok = bb < len(tb)
        typ = np.where(ok, tb[np.where(ok, bb, 0)], FILLER_TYPE)
        col = COLLISION[typ]
        act = ACT[typ]
        receive = ok & (col == 0) & np.isin(act, RECEIVE_ACTS)
        solid = ok & (col != 0) & ~np.isin(act, NO_CAST_ACTS)
        wet = ok & np.isin(act, NO_CAST_ACTS + (ACT_PIT,))
        return eb, cb, et, ct, receive, solid, wet

    def palette_rgb(self, ts):
        pal = np.zeros((16, 16, 3), dtype=np.int64)
        for r, cols in ts.rows.items():
            pal[r] = [owdata.gba_to_rgb(c) for c in cols]
        return pal

    def ground_colors(self, ts):
        """{15-bit color: pixels} of plain ground in this tileset's rooms."""
        counts = {}
        pal = self.palette_rgb(ts)
        for i, w, h, b, t in ts.rooms:
            eb, cb, et, ct, receive, solid, wet = self.room_layers(ts, w, h, b, t)
            m = np.kron(receive, np.ones((16, 16), dtype=bool)) & (cb != 0) & (ct == 0)
            rgb = pal[(eb >> 12)[m], cb[m]]
            v = (rgb[:, 0] >> 3) | ((rgb[:, 1] >> 3) << 5) | ((rgb[:, 2] >> 3) << 10)
            vals, n = np.unique(v, return_counts=True)
            for a, c in zip(vals, n):
                counts[int(a)] = counts.get(int(a), 0) + int(c)
        return counts

    def ground_keys(self, ts):
        counts = self.ground_colors(ts)
        total = max(1, sum(counts.values()))
        return set(k for k, c in counts.items() if c > 0.002 * total)

    def preview(self, ts_index, room, styled=True):
        ts = self.tilesets[ts_index]
        for i, w, h, b, t in ts.rooms:
            if i != room:
                continue
            pal = ts.area.palette()
            if styled:
                for r, cols in ts.rows.items():
                    pal[r] = [owdata.gba_to_rgb(c) for c in cols]
            return owdata.render_room(ts.area, b, t, w, h, pal)
        return None
