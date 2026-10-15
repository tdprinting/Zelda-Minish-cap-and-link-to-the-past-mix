"""
Dungeon fronts and landmarks of A Link to the Past, built into Minish Cap's overworld.

Each site names a Minish Cap overworld room, the world it shows in (Light, Dark or
both), the door the dungeon's way in already uses (its transition), and a piece of A
Link to the Past's overworld: the Eastern Palace's front, the Palace of Darkness,
the skull of Skull Woods and so on. The piece is cut out of A Link to the Past's own
map, re-lit in the blended style (dungeons/blendstyle.py, dungeons/celshade.py), and
laid into the room with its door on the Minish Cap door:

  * its plain ground (grass, earth) is left out, so Minish Cap's ground runs under it;
  * every 8x8 character is quantized into one or two palette rows that the room and
    its neighbours never use, and duplicates (also mirrored ones) are stored once;
  * the characters go into character slots no metatile of the area uses, the new
    metatiles into the area's unused (filler) metatile slots;
  * collision comes from A Link to the Past's own tile attributes: ground is
    walkable, walls and statues are solid, the door is a Minish Cap cave mouth.

At run time (tmc/src/blendOverworld.c) the room loads as usual, then the site's
characters, colors, metatiles and room map are laid over it - only in the world the
site belongs to - and its door shows sealed until the dungeon can be entered.
"""
import os
import sys

import numpy as np

import alttpow
import owdata
from owdata import FILLER_TYPE, TOP_BASE

sys.path.insert(0, os.path.join(owdata.HERE, '..', 'dungeons'))
import blendstyle  # noqa: E402
import celshade  # noqa: E402
import mctiles  # noqa: E402

COLLISION = mctiles.COLLISION + [0] * (4096 - len(mctiles.COLLISION))
ACT = mctiles.ACT + [0] * (4096 - len(mctiles.ACT))
TILE_CAVE_MOUTH = 181          # TILE_TYPE_181: a cave's open mouth (bombable walls open into it)

LIGHT, DARK, BOTH = 1, 2, 3


class Site:
    def __init__(self, name, area, room, world, door, src, src_door, dungeon=0, cave_flag=0,
                 drop=(), keep=(), ground=8, rows=2, door_cells=None, sealed_from=None, shade=True):
        self.name = name
        self.area, self.room, self.world = area, room, world
        self.door = door                  # the transition's (x, y) in the room, pixels
        self.src = src                    # (ALttP area, x0, y0, x1, y1) in map16 cells
        self.src_door = src_door          # the ALttP door's map16 cell
        self.dungeon = dungeon            # dungeon area behind the door (for its seal)
        self.cave_flag = cave_flag        # overworld local flag that opens it, if any
        self.drop = drop                  # ALttP cell rects (x0, y0, x1, y1) left out
        self.keep = keep                  # ALttP cell rects kept even where they look like ground
        self.ground = ground              # how many of the area's commonest map16 count as plain ground
        self.rows = rows                  # palette rows to use at most
        self.door_cells = door_cells      # ALttP cells that form the doorway (default: the door cell)
        self.sealed_from = sealed_from    # ALttP cell whose art closes the doorway while sealed
        self.shade = shade                # cel-shade the art (outlines, rim light)


# Where each A Link to the Past dungeon meets the Minish Cap overworld. The doors are
# the transitions in tmc/src/data/transitions.c that lead into the dungeon areas.
SITES = [
    Site('EasternPalace', 0x03, 3, LIGHT, (0xa8, 0x98), (0x1e, 46, 0, 61, 7), (53, 3),
         dungeon=0x0e, cave_flag=0x70),
]


# ---------------------------------------------------------------- the Minish Cap side

def room_header(area_index, room):
    t = owdata.area_tables()[area_index]
    label = t['headers'].replace('gAreaRoomHeaders_', '')
    src = owdata.asm('data/map/room_headers.s')
    start = src.index('gAreaRoomHeaders_%s::' % label)
    rows = []
    for line in src[start:].split('\n')[1:]:
        line = line.strip()
        if not line.startswith('room_header'):
            break
        rows.append([int(v, 0) for v in line.split(None, 1)[1].split(',')])
    return rows


class McRoom:
    """A Minish Cap overworld room with its tileset, and what its neighbours use."""

    def __init__(self, area_index, room, palettes=None):
        self.index, self.room = area_index, room
        hdrs = room_header(area_index, room)
        x, y, pw, ph, ts = hdrs[room]
        self.w, self.h, self.ts = pw // 16, ph // 16, ts
        self.area = owdata.Area(area_index, ts)
        if palettes:
            for r in list(self.area.rows):
                pid = self.area.rows[r][0]
                if pid in palettes:
                    self.area.rows[r] = (pid, palettes[pid])
        maps = {i: (b, t) for i, lab, b, t in self.area.maps}
        b, t = maps[room]
        self.bottom = owdata.read_u16(owdata.raw_path(b))[:self.w * self.h].copy()
        self.top = owdata.read_u16(owdata.raw_path(t))[:self.w * self.h].copy()
        # neighbours: rooms of the area whose rectangle touches this one
        self.neighbours = [i for i, (X, Y, W, H, T) in enumerate(hdrs)
                           if T != 0xffff and not (X * 16 > x * 16 + pw or X * 16 + W < x * 16 or
                                                   Y * 16 > y * 16 + ph or Y * 16 + H < y * 16)]
        self.hdrs = hdrs
        self.maps = maps

    def used(self, rooms):
        """Characters and palette rows the metatiles of these rooms use."""
        chars, rows = set(), set()
        for i in rooms:
            if i not in self.maps or self.maps[i][0] is None:
                continue
            W, H = self.hdrs[i][2] // 16, self.hdrs[i][3] // 16
            b, t = self.maps[i]
            for layer, sym, base in (('bottom', b, 0), ('top', t, TOP_BASE)):
                cells = owdata.read_u16(owdata.raw_path(sym))[:W * H]
                meta = self.area.meta[layer]
                for m in set(int(c) for c in cells):
                    if m * 4 + 3 >= len(meta) or (layer == 'top' and m == 0):
                        continue
                    for k in range(4):
                        e = int(meta[m * 4 + k])
                        chars.add((e & 0x3ff) + base)
                        rows.add(e >> 12)
        return chars, rows

    def reserved_chars(self):
        """Characters the area's tile animations write, and those its neighbours' own
        room maps load graphics into (a neighbour's map can carry a tileset chunk)."""
        out = set()
        tables = owdata.area_tables()
        for i, t in enumerate(tables):
            if t['tilesets'] == tables[self.index]['tilesets']:
                for aid, frames in owdata.area_animations(i):
                    for fr in frames:
                        for vram, size, is_pal, sym in fr:
                            if not is_pal:
                                out |= set(range(vram // 32, vram // 32 + size))
        src = owdata.asm('data/map/map_headers.s')
        labels = owdata.tileset_list_from(src, tables[self.index]['maps'])
        for i in self.neighbours:
            if i == self.room or i >= len(labels):
                continue
            try:
                block = owdata.label_block(src, labels[i])
            except ValueError:
                continue
            for l in block:
                if l.startswith('tileSet_tiles'):
                    a = [x.strip() for x in l.split(None, 1)[1].split(',')]
                    dest, size = int(a[1], 0), int(a[2], 0)
                    out |= set(range((dest - 0x6000000) // 32, (dest - 0x6000000 + size) // 32))
        return out

    def render(self, bottom=None, top=None, meta_b=None, chars=None, pal=None):
        pal = self.palette() if pal is None else pal
        return owdata.render_room(self.area, self.bottom if bottom is None else bottom,
                                  self.top if top is None else top, self.w, self.h, pal,
                                  chars=chars, meta_b=meta_b)

    def palette(self):
        return self.area.palette()


# ---------------------------------------------------------------- color

def style_colors(img):
    """A Link to the Past's colors re-lit the Minish Cap way and flattened into bands."""
    flat = img.reshape(-1, 3)
    uniq = sorted(set(map(tuple, flat.tolist())))
    lit = {c: blendstyle.blend_color(c) for c in uniq}
    bands = celshade.flatten_tones(sorted(set(lit.values())))
    lut = {c: bands[lit[c]] for c in uniq}
    out = np.array([lut[tuple(c)] for c in flat.tolist()], dtype=np.int64)
    return out.reshape(img.shape)


def median_cut(colors, weights, n):
    """n representative colors for weighted RGB colors."""
    boxes = [(np.array(colors, dtype=float), np.array(weights, dtype=float))]
    while len(boxes) < n:
        boxes.sort(key=lambda b: -(b[1].sum() * (np.ptp(b[0], axis=0).max() if len(b[0]) > 1 else 0)))
        c, w = boxes.pop(0)
        if len(c) < 2:
            boxes.append((c, w))
            break
        ax = int(np.argmax(np.ptp(c, axis=0)))
        order = np.argsort(c[:, ax])
        c, w = c[order], w[order]
        cut = int(np.searchsorted(np.cumsum(w), w.sum() / 2))
        cut = min(max(cut, 1), len(c) - 1)
        boxes += [(c[:cut], w[:cut]), (c[cut:], w[cut:])]
    out = []
    for c, w in boxes:
        out.append(tuple(int(v) for v in celshade.snap((c * w[:, None]).sum(0) / w.sum())))
    return sorted(set(out))


def nearest(block, palette):
    """block (..., 3) -> (indices into palette, squared error)."""
    p = np.array(palette, dtype=np.int64)
    d = ((block[..., None, :] - p) ** 2).sum(-1)
    return d.argmin(-1), d.min(-1)


def quantize(chars, nrows, max_colors=15):
    """chars: list of 8x8x3 arrays -> (rows: [[15 colors]], row of each char, index pixels)."""
    means = np.array([c.reshape(-1, 3).mean(0) for c in chars])
    # start: split the characters by lightness-weighted hue into nrows groups
    key = means @ np.array([0.5, 0.3, 0.2]) if nrows > 1 else np.zeros(len(chars))
    group = np.zeros(len(chars), dtype=int)
    if nrows > 1:
        qs = np.quantile(key, np.linspace(0, 1, nrows + 1)[1:-1])
        group = np.searchsorted(qs, key)
    pals = []
    for it in range(6):
        pals = []
        for g in range(nrows):
            px = [c.reshape(-1, 3) for c, gg in zip(chars, group) if gg == g]
            if not px:
                pals.append([(0, 0, 0)])
                continue
            px = np.concatenate(px)
            u, n = np.unique(px, axis=0, return_counts=True)
            pals.append(median_cut([tuple(v) for v in u], n, max_colors))
        errs = np.array([[nearest(c.reshape(-1, 3), p)[1].sum() for p in pals] for c in chars])
        new = errs.argmin(1)
        if (new == group).all():
            break
        group = new
    idx = [nearest(c.reshape(-1, 3), pals[g])[0].reshape(8, 8) + 1 for c, g in zip(chars, group)]
    return pals, group, idx


# ---------------------------------------------------------------- building a site

class Built:
    """One site's output: what blendOverworld.c lays over the room."""

    def __init__(self, site, mc):
        self.site, self.mc = site, mc
        self.char_slots = []      # character numbers (VRAM / 32) in the order of self.chars
        self.chars = []           # 8x8 index arrays
        self.rows = {}            # palette row -> 16 GBA colors
        self.meta_slots = []      # bottom metatile numbers
        self.metas = []           # [4 entries]
        self.types = []
        self.bottom = mc.bottom.copy()
        self.top = mc.top.copy()
        self.doors = []           # (packed cell, sealed metatile)


def ground_cells(src, site, x0, y0, w, h):
    """ALttP cells in the crop that are plain ground (left out, so Minish Cap's ground shows)."""
    common = src.common_map16(site.ground)
    g = np.zeros((h, w), dtype=bool)
    for y in range(h):
        for x in range(w):
            m = int(src.map[y0 + y, x0 + x])
            at = src.attrs(x0 + x, y0 + y)
            g[y, x] = m in common and all(a in alttpow.WALKABLE for a in at)
    for rx0, ry0, rx1, ry1 in site.drop:
        g[max(0, ry0 - y0):max(0, ry1 - y0), max(0, rx0 - x0):max(0, rx1 - x0)] = True
    for rx0, ry0, rx1, ry1 in site.keep:
        g[max(0, ry0 - y0):max(0, ry1 - y0), max(0, rx0 - x0):max(0, rx1 - x0)] = False
    return g


def pick_types(mc):
    """The room's commonest walkable ground type and commonest solid type."""
    tb = mc.area.types['bottom']
    walk, solid = {}, {}
    for c in mc.bottom:
        c = int(c)
        if c >= len(tb):
            continue
        t = int(tb[c])
        if COLLISION[t] == 0 and ACT[t] in (9, 10, 11, 12):
            walk[t] = walk.get(t, 0) + 1
        elif COLLISION[t] == 0x0f and ACT[t] == 0:
            solid[t] = solid.get(t, 0) + 1
    w = max(walk, key=walk.get) if walk else 0
    s = max(solid, key=solid.get) if solid else 0x0c
    return w, s


def build_site(site, palettes, char_pool, log=print):
    mc = McRoom(site.area, site.room, palettes)
    out = Built(site, mc)
    scr, sx0, sy0, sx1, sy1 = site.src
    src = alttpow.Area(scr)
    w, h = sx1 - sx0, sy1 - sy0
    # place: the ALttP door cell onto the Minish Cap door cell
    dcx, dcy = site.door[0] // 16, site.door[1] // 16
    ox, oy = dcx - (site.src_door[0] - sx0), dcy - (site.src_door[1] - sy0)
    ground = ground_cells(src, site, sx0, sy0, w, h)
    art = style_colors(src.render(sx0, sy0, w, h))
    if site.shade:
        cast = np.kron(~ground, np.ones((16, 16), dtype=bool))
        art = celshade.shade(art.astype(float), np.zeros(cast.shape, dtype=bool),
                             np.zeros(cast.shape, dtype=bool), cast).astype(np.int64)
    walk_t, solid_t = pick_types(mc)

    cells = []                  # (mc x, my, alttp x, y)
    for y in range(h):
        for x in range(w):
            if ground[y, x]:
                continue
            mx, my = ox + x, oy + y
            if 0 <= mx < mc.w and 0 <= my < mc.h:
                cells.append((mx, my, x, y))
    # characters of every placed cell
    tiles = []
    for mx, my, x, y in cells:
        for k in range(4):
            py, px = y * 16 + (k >> 1) * 8, x * 16 + (k & 1) * 8
            tiles.append(art[py:py + 8, px:px + 8])
    used_chars, used_rows = mc.used(mc.neighbours)
    used_chars |= mc.reserved_chars()
    free_rows = [r for r in range(2, 15) if r not in used_rows]
    nrows = min(site.rows, len(free_rows))
    if nrows == 0:
        raise SystemExit('%s: no free palette row' % site.name)
    pals, group, idx = quantize(tiles, nrows)
    rows = free_rows[:nrows]
    for r, p in zip(rows, pals):
        cols = [owdata.rgb_to_gba(c) for c in p]
        out.rows[r] = [0] + cols + [0] * (15 - len(cols))
    # dedupe characters (mirrored ones too) and give each a free slot
    store = {}
    free_slots = [c for c in char_pool if c not in used_chars and c < 1024]
    entries = []
    for t, g in zip(idx, group):
        t = t.astype(np.uint8)
        hit = None
        for hf in (0, 1):
            for vf in (0, 1):
                v = t[:, ::-1] if hf else t
                v = v[::-1, :] if vf else v
                key = v.tobytes()
                if key in store:
                    hit = (store[key], hf, vf)
                    break
            if hit:
                break
        if hit is None:
            if not free_slots:
                raise SystemExit('%s: out of character slots' % site.name)
            slot = free_slots.pop(0)
            char_pool.remove(slot)
            store[t.tobytes()] = slot
            out.char_slots.append(slot)
            out.chars.append(t)
            hit = (slot, 0, 0)
        slot, hf, vf = hit
        entries.append(slot | (hf << 10) | (vf << 11) | (rows[g] << 12))
    # metatiles into the filler slots
    tb = mc.area.types['bottom']
    filler = [i for i in range(len(mc.area.meta['bottom']) // 4) if i < len(tb) and tb[i] == FILLER_TYPE and i > 0]
    door_cells = set(site.door_cells or [site.src_door])
    for n, (mx, my, x, y) in enumerate(cells):
        ent = entries[n * 4:n * 4 + 4]
        at = src.attrs(sx0 + x, sy0 + y)
        if (sx0 + x, sy0 + y) in door_cells:
            typ = TILE_CAVE_MOUTH
        elif all(a in alttpow.WALKABLE for a in at):
            typ = walk_t
        else:
            typ = solid_t
        key = (tuple(ent), typ)
        if key in out.metas_index if hasattr(out, 'metas_index') else False:
            m = out.metas_index[key]
        else:
            if not hasattr(out, 'metas_index'):
                out.metas_index = {}
            m = filler.pop(0)
            out.metas_index[key] = m
            out.meta_slots.append(m)
            out.metas.append(ent)
            out.types.append(typ)
        out.bottom[my * mc.w + mx] = m
        out.top[my * mc.w + mx] = 0
    # the sealed doorway: the art of the cell the site names, solid
    if site.dungeon or site.cave_flag:
        sx, sy = site.sealed_from or (site.src_door[0], site.src_door[1] - 1)
        sealed = int(out.bottom[(oy + sy - sy0) * mc.w + ox + sx - sx0])
        sealed_key = None
        for key, m in out.metas_index.items():
            if m == sealed:
                sealed_key = key
        if sealed_key is not None:
            m = filler.pop(0)
            out.meta_slots.append(m)
            out.metas.append(list(sealed_key[0]))
            out.types.append(solid_t)
            for dx, dy in door_cells:
                cell = (oy + dy - sy0) * mc.w + ox + dx - sx0
                out.doors.append((cell, m))
    log('  %s: %d cells, %d characters, %d metatiles, rows %s' % (
        site.name, len(cells), len(out.chars), len(out.metas), rows))
    return out


def preview(built):
    """RGB of the room as the site makes it (for screenshot sheets and checks)."""
    mc = built.mc
    chars = mc.area.chars.copy()
    for slot, t in zip(built.char_slots, built.chars):
        chars[slot] = t
    meta = mc.area.meta['bottom'].copy()
    for m, ent in zip(built.meta_slots, built.metas):
        meta[m * 4:m * 4 + 4] = ent
    pal = mc.palette()
    for r, cols in built.rows.items():
        pal[r] = [owdata.gba_to_rgb(c) for c in cols]
    return owdata.render_room(mc.area, built.bottom, built.top, mc.w, mc.h, pal, chars=chars, meta_b=meta)


# ---------------------------------------------------------------- output

def write(builts, out_dir, log=print):
    """exteriors.inc (included by data/map/map_data.s) and its binaries."""
    import struct
    L = ['@ A Link to the Past dungeon fronts on the overworld, made by blend/overworld/exteriors.py',
         '@ from your own ROMs. Do not edit or commit.', '\t.align 2', 'gBlendExteriors::']
    for n, b in enumerate(builts):
        s, mc = b.site, b.mc
        mask = sum(1 << r for r in b.rows)
        L.append('\t@ %s' % s.name)
        L.append('\t.byte 0x%02x, %d, %d, %d' % (s.area, s.room, s.world, len(b.doors)))
        L.append('\t.2byte 0x%04x, %d, %d, %d' % (mask, len(b.metas), len(b.chars), mc.w * mc.h))
        L.append('\t.4byte ' + ', '.join('gBlendExt%d_%s' % (n, k) for k in
                                         ('chars', 'slots', 'pal', 'metaslots', 'metas', 'types', 'bottom', 'top', 'doors', 'nb')))
    L.append('\t.byte 0xff, 0, 0, 0')
    L.append('\t.2byte 0, 0, 0, 0')
    L.append('\t.4byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0')
    for n, b in enumerate(builts):
        s, mc = b.site, b.mc
        base = 'ext_%d_%s' % (n, s.name)
        px = np.array(b.chars, dtype=np.uint8) if b.chars else np.zeros((0, 8, 8), np.uint8)
        open(os.path.join(out_dir, base + '_chars.bin'), 'wb').write(owdata.encode_chars(px))
        pal = b''.join(struct.pack('<16H', *b.rows[r]) for r in sorted(b.rows))
        open(os.path.join(out_dir, base + '_pal.bin'), 'wb').write(pal)
        open(os.path.join(out_dir, base + '_bottom.bin'), 'wb').write(np.asarray(b.bottom, '<u2').tobytes())
        open(os.path.join(out_dir, base + '_top.bin'), 'wb').write(np.asarray(b.top, '<u2').tobytes())
        meta = [e for m in b.metas for e in m]
        L += ['\t.align 2', 'gBlendExt%d_chars::' % n, '\t.incbin "custom/gen/ow/%s_chars.bin"' % base]
        L += ['\t.align 2', 'gBlendExt%d_pal::' % n, '\t.incbin "custom/gen/ow/%s_pal.bin"' % base]
        L += ['\t.align 2', 'gBlendExt%d_bottom::' % n, '\t.incbin "custom/gen/ow/%s_bottom.bin"' % base]
        L += ['\t.align 2', 'gBlendExt%d_top::' % n, '\t.incbin "custom/gen/ow/%s_top.bin"' % base]
        for k, vals in (('slots', b.char_slots), ('metaslots', b.meta_slots), ('metas', meta), ('types', b.types)):
            L += ['\t.align 2', 'gBlendExt%d_%s::' % (n, k)]
            for i in range(0, len(vals), 16):
                L.append('\t.2byte ' + ', '.join(str(int(v)) for v in vals[i:i + 16]))
        L += ['\t.align 2', 'gBlendExt%d_doors::' % n]
        for cell, sealed in b.doors:
            L.append('\t.2byte %d, %d' % (cell, sealed))
            L.append('\t.byte 0x%02x, 0x%02x' % (s.dungeon, s.cave_flag))
            L.append('\t.2byte 0')
        L += ['gBlendExt%d_nb::' % n, '\t.byte ' + ', '.join(str(i) for i in mc.neighbours) + ', 0xff']
    L.append('\t.align 2')
    open(os.path.join(out_dir, 'exteriors.inc'), 'w').write('\n'.join(L) + '\n')
    log('Wrote %d dungeon fronts to %s' % (len(builts), out_dir))
