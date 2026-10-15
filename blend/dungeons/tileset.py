"""
The blended dungeon tileset: each room is drawn with A Link to the Past's own
architecture (its two background layers flattened into one), re-lit in Minish
Cap's colors by blendstyle and given a cel-shaded finish with soft shadows by
celshade, with Minish Cap's chests on top. Collision still comes from the Minish
Cap tile types the autotiler picked for the room plan, so the rooms play exactly
as they did with the Minish Cap tiles.

Rooms are shaded in full color; write() then packs the colors into Game Boy
Advance palette rows, cuts the rooms into 8px characters and writes the 32KB
character block, the palettes and the metatile and tile type tables, plus the
headers that load them (into maps.inc).
"""
import os
import struct

import numpy as np

import alttprooms as A
import alttpgfx
import blendstyle
import celshade
import mcrender
import mctiles

# tiles 0 and 512 stay blank: 0 for the bottom layer, 512 is tile 0 of the top
# layer (its characters start 16KB in), which is left empty
FREE_TILES = [i for i in range(1, 1024) if i != 512]
# palette rows for the tileset (row 0 is Minish Cap's own, which its chests use)
GBA_ROWS = list(range(2, 15))
MAX_METATILES = 2048
# the space outside a room's walls
VOID = (40, 32, 24)

# A Link to the Past classes that are drawn as plain floor wherever the plan lets
# Link walk: doors the Minish Cap door objects now draw, blocks and pots the plan
# dropped, and A Link to the Past chests (Minish Cap ones replace them)
REDRAW_AS_FLOOR = (A.LOCKED, A.SHUTTER, A.ENTRANCE, A.BLOCK, A.POT, A.CHEST, A.BIG_CHEST,
                   A.SOLID, A.TORCH, A.PIT, A.WATER)
PASSABLE = (A.FLOOR, '+')
# what casts a shadow, and what a shadow falls on
CASTS = (A.SOLID, A.TORCH, A.BLOCK, A.POT, A.LOCKED, A.SHUTTER)
RECEIVES = (A.FLOOR, A.STAIRS)


def gba_tile(pix):
    out = bytearray(32)
    for y in range(8):
        for x in range(0, 8, 2):
            out[y * 4 + x // 2] = (pix[y][x] & 15) | ((pix[y][x + 1] & 15) << 4)
    return bytes(out)


def flips(pix):
    h = tuple(r[::-1] for r in pix)
    return [(pix, 0), (h, 0x400), (pix[::-1], 0x800), (h[::-1], 0xc00)]


def color_distance(a, b):
    # weighted RGB, close enough to perceptual for merging nearby tones
    dr, dg, db = a[0] - b[0], a[1] - b[1], a[2] - b[2]
    return 2 * dr * dr + 4 * dg * dg + 3 * db * db


def pack_palettes(sets, rows, size=15):
    """Assigns each color set (one per tile) to a palette of at most `size` colors.
    Returns (palettes, index of each set's palette) or None if more than `rows` are needed."""
    order = sorted(range(len(sets)), key=lambda i: -len(sets[i]))
    pals, where = [], [None] * len(sets)
    for i in order:
        s = sets[i]
        best, best_add = None, None
        for k, p in enumerate(pals):
            add = len(s - p)
            if len(p) + add <= size and (best is None or add < best_add):
                best, best_add = k, add
        if best is None:
            if len(pals) == rows:
                return None
            pals.append(set(s))
            best = len(pals) - 1
        else:
            pals[best] |= s
        where[i] = best
    return pals, where


class Blend:
    def __init__(self, name, tileset_area='043'):
        self.name = name
        self.types_b = mctiles.tile_types(tileset_area, 'bottom')
        self.types_t = mctiles.tile_types(tileset_area, 'top')
        self.mc_meta = mcrender.metatiles(tileset_area, 'bottom')
        self.mc_chars = mcrender.gfx(tileset_area)
        self.tiles = {}           # 8px tile (RGB pixels, None where empty) -> how often it is used
        self.meta_b, self.meta_t = {}, {}   # key -> metatile index
        self.colors = None
        self.tones = None

    # rooms

    def rgb(self, c):
        return self.tones[blendstyle.blend_color(alttpgfx.bgr555(self.colors[c]))]

    def compose(self, gfx):
        """The room as one picture, in blended, banded colors. A Link to the Past's main
        layer is the first tilemap (walls, floors, objects); the second layer is shown
        behind it (a floor seen through gaps, the level below a walkway, a parallax
        floor) or blended in, so it goes underneath. Also notes, per 8px tile, whether
        the second layer is the one seen there."""
        img = np.zeros((512, 512, 3))
        empty = np.ones((512, 512), dtype=bool)
        self.hi_seen = np.zeros((64, 64), dtype=bool)
        lut = np.array([self.rgb(c) if c else (0, 0, 0) for c in range(256)], dtype=float)
        for i in range(4096):
            lo, hi = np.array(gfx.tile(gfx.bg2[i])), np.array(gfx.tile(gfx.bg1[i]))
            pix = np.where(lo != 0, lo, hi)
            tx, ty = (i % 64) * 8, (i // 64) * 8
            img[ty:ty + 8, tx:tx + 8] = lut[pix]
            empty[ty:ty + 8, tx:tx + 8] = pix == 0
            self.hi_seen[i // 64, i % 64] = ((lo == 0) & (hi != 0)).sum() * 2 > (pix != 0).sum()
        return img, empty

    def room(self, plan, bottom, top):
        """Redraws one room: bottom/top are the autotiler's Minish Cap metatiles,
        whose tile types are kept. Returns the room's new bottom and top cells."""
        gfx = alttpgfx.RoomGfx(plan.id)
        if self.colors is None:
            self.colors = gfx.colors
            used = {blendstyle.blend_color(alttpgfx.bgr555(c)) for c in self.colors[:128]}
            self.tones = celshade.flatten_tones(sorted(used))
        elif gfx.colors[:128] != self.colors[:128]:
            print('%s: room %03x has other colors; using the first room\'s' % (self.name, plan.id))
        if not self.meta_t:
            # top layer: empty, with the tile types the autotiler chose
            self.metatile(self.meta_t, self.types_t[0])
        alttp = plan.room.cells
        img, empty = self.compose(gfx)
        self.restone(gfx, plan, img, empty)

        # where the plan opens a way through A Link to the Past's art, draw floor
        floor = [(i % 32, i // 32) for i in range(1024) if alttp[i] == A.FLOOR and plan.cells[i] == A.FLOOR]
        cls = np.array([[A.attr_class(plan.room.attr[y * 64 + x]) for x in range(64)] for y in range(64)])
        cls = cls.repeat(8, axis=0).repeat(8, axis=1)
        for i in range(1024):
            if plan.cells[i] in PASSABLE and alttp[i] in REDRAW_AS_FLOOR and floor:
                cx, cy = i % 32, i // 32
                sx, sy = min(floor, key=lambda p: abs(p[0] - cx) + abs(p[1] - cy))
                dst = (slice(cy * 16, cy * 16 + 16), slice(cx * 16, cx * 16 + 16))
                src = (slice(sy * 16, sy * 16 + 16), slice(sx * 16, sx * 16 + 16))
                img[dst] = img[src]
                empty[dst] = empty[src]
                cls[dst] = A.FLOOR
        # shadows only fall where Link can go: A Link to the Past leaves unseen floor and
        # invisible walls around its rooms
        walk = np.array([[plan.cells[(y // 16) * 32 + x // 16] in PASSABLE for x in range(512)] for y in range(512)])
        receive = np.isin(cls, RECEIVES) & walk
        cast = np.isin(cls, CASTS) & ~walk
        img = celshade.shade(img, empty, receive, cast)

        new_b, new_t = [], []
        for i in range(1024):
            cx, cy = i % 32, i // 32
            b, t = bottom[i], top[i]
            new_t.append(t if t >= 0x4000 else self.metatile(self.meta_t, self.types_t[t]))
            if b >= 0x4000:
                new_b.append(b)
                continue
            tile_type = self.types_b[b]
            if tile_type in (0x73, 0x74):
                # Minish Cap chests, so they open the Minish Cap way
                key = ('mc', b)
            else:
                key = tuple(self.tile_key(img, empty, cx * 16 + dx * 8, cy * 16 + dy * 8)
                            for dy in (0, 1) for dx in (0, 1))
            new_b.append(self.metatile(self.meta_b, (key, tile_type)))
        # the open chest must exist for a chest to open into
        for b in range(len(self.types_b)):
            if self.types_b[b] == 0x74:
                self.metatile(self.meta_b, (('mc', b), 0x74))
                break
        return new_b, new_t

    def restone(self, gfx, plan, img, empty):
        """Swaps A Link to the Past's patterned floors for calm stone slabs: big 16px
        squares with a lit top-left edge and a shaded joint, in each floor's own color.
        A Link to the Past lays a floor as a 4x4-tile pattern repeated across a layer
        before drawing anything on top, so a tile is floor where its layer repeats
        4 tiles across and 4 tiles down; drawings, walls and steps keep their art."""
        def pattern(layer):
            words = np.array(layer).reshape(64, 64) & 0xffff
            p = np.zeros((64, 64), dtype=bool)
            for dy, dx in ((0, 4), (0, -4)):
                for vy in (4, -4):
                    h = np.roll(words, (0, dx), axis=(0, 1)) == words
                    v = np.roll(words, (vy, 0), axis=(0, 1)) == words
                    p |= h & v
            # then every tile of those pattern words, also where objects break the repeat
            return np.isin(words, np.unique(words[p])) if p.any() else p
        empty_hi = ~self.hi_seen
        # floor on the layer the room was planned from (the other is often blank,
        # which reads as floor everywhere, walls included)
        floor = np.array([A.attr_class(a) == A.FLOOR for a in plan.room.attr]).reshape(64, 64)
        # and the patterned fill around the rooms, where Link can't go
        outside = np.array([[plan.cells[(y // 2) * 32 + x // 2] not in PASSABLE for x in range(64)]
                            for y in range(64)])
        floor |= outside
        on_hi = pattern(gfx.bg1) & ~empty_hi & floor
        on_lo = pattern(gfx.bg2) & empty_hi & floor
        yy, xx = np.mgrid[0:512, 0:512]
        for tiles in (on_lo & ~outside, on_lo & outside, on_hi & ~outside, on_hi & outside):
            # each stretch of floor gets its own color
            seen = np.zeros((64, 64), dtype=bool)
            for sy in range(64):
                for sx in range(64):
                    if not tiles[sy, sx] or seen[sy, sx]:
                        continue
                    comp, stack = [], [(sy, sx)]
                    seen[sy, sx] = True
                    while stack:
                        y, x = stack.pop()
                        comp.append((y, x))
                        for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                            if 0 <= ny < 64 and 0 <= nx < 64 and tiles[ny, nx] and not seen[ny, nx]:
                                seen[ny, nx] = True
                                stack.append((ny, nx))
                    if len(comp) < 4:
                        continue
                    part = np.zeros((64, 64), dtype=bool)
                    for y, x in comp:
                        part[y, x] = True
                    mask = part.repeat(8, axis=0).repeat(8, axis=1) & ~empty
                    if not mask.any():
                        continue
                    walk = sum(plan.cells[(y // 2) * 32 + x // 2] in PASSABLE for y, x in comp)
                    if walk * 2 < len(comp):
                        if len(comp) < 16:
                            continue  # a patterned bit of wall, not the space around
                        # the space around the room, where Link can't go: plain darkness,
                        # as in Minish Cap (a floor may run on to the room's edge where
                        # A Link to the Past joins rooms with no door)
                        img[mask] = VOID
                        continue
                    base = celshade.snap(np.median(img[mask], axis=0))
                    stone = np.empty((512, 512, 3))
                    stone[:] = base
                    stone[((xx // 16 + yy // 16) % 2) == 1] = celshade.snap(base * 0.95)
                    stone[(xx % 16 == 0) | (yy % 16 == 0)] = celshade.snap(base + (255 - base) * 0.16)
                    stone[(xx % 16 == 15) | (yy % 16 == 15)] = celshade.snap(base * 0.82)
                    img[mask] = stone[mask]

    def tile_key(self, img, empty, x, y):
        pix = tuple(tuple(None if empty[y + j, x + i] else tuple(int(v) for v in img[y + j, x + i])
                          for i in range(8)) for j in range(8))
        self.tiles[pix] = self.tiles.get(pix, 0) + 1
        return pix

    def metatile(self, table, key):
        if key not in table:
            if len(table) >= MAX_METATILES:
                raise RuntimeError('%s: more than %d metatiles' % (self.name, MAX_METATILES))
            table[key] = len(table)
        return table[key]

    # output

    def reduce(self, budget):
        """Fits the room art into `budget` characters: flipped copies share one, then the
        least-used tiles give way to the closest-looking ones. Returns the tiles kept and
        {tile: (kept tile, flip bits)}."""
        canon, use = {}, {}
        for t, n in self.tiles.items():
            c, bits = min(flips(t), key=lambda v: repr(v[0]))
            canon[t] = (c, bits)
            use[c] = use.get(c, 0) + n
        kept = sorted(use, key=lambda c: -use[c])
        if len(kept) > budget:
            arr = np.array([[[-200] * 3 if p is None else p for r in c for p in r] for c in kept], dtype=np.float32)
            arr = arr.reshape(len(kept), 8, 8, 3)
            var = [arr, arr[:, :, ::-1], arr[:, ::-1, :], arr[:, ::-1, ::-1]]
            n = len(kept)
            dist = np.full((n, n), np.inf, dtype=np.float32)
            how = np.zeros((n, n), dtype=np.int8)
            flat = arr.reshape(n, -1)
            for f, v in enumerate(var):
                vf = v.reshape(n, -1)
                for i in range(0, n, 128):
                    d = ((flat[i:i + 128, None, :] - vf[None, :, :]) ** 2).sum(-1)
                    better = d < dist[i:i + 128]
                    dist[i:i + 128][better] = d[better]
                    how[i:i + 128][better] = f
            np.fill_diagonal(dist, np.inf)
            alive = np.ones(n, dtype=bool)
            weight = np.array([use[c] for c in kept], dtype=np.float64)
            target = {}
            left = n
            while left > budget:
                d = np.where(alive[None, :], dist, np.inf).min(axis=1)
                cost = np.where(alive, d * weight, np.inf)
                a = int(cost.argmin())
                b = int(np.where(alive, dist[a], np.inf).argmin())
                alive[a] = False
                weight[b] += weight[a]
                target[a] = (b, (0, 0x400, 0x800, 0xc00)[how[a, b]])
                left -= 1

            def resolve(i):
                bits = 0
                while i in target:
                    i, f = target[i]
                    bits ^= f
                return i, bits
            remap = {kept[i]: (kept[j], f) for i in range(n) for j, f in [resolve(i)]}
            kept = [kept[i] for i in range(n) if alive[i]]
        else:
            remap = {c: (c, 0) for c in kept}
        final = {}
        for t, (c, bits) in canon.items():
            k, f = remap[c]
            final[t] = (k, bits ^ f)
        return kept, final

    def quantize(self, kept):
        """Fits the colors into the palette rows, merging the closest tones until they fit."""
        merged = {}
        while True:
            sets = [frozenset(merged.get(c, c) for r in t for c in r if c is not None) for t in kept]
            packed = pack_palettes(sets, len(GBA_ROWS))
            if packed:
                return merged, sets, packed
            colors = sorted({c for s in sets for c in s})
            best = None
            for i, a in enumerate(colors):
                for b in colors[i + 1:]:
                    d = color_distance(a, b)
                    if best is None or d < best[0]:
                        best = (d, a, b)
            _, a, b = best
            keep, drop = (a, b) if sum(a) < sum(b) else (b, a)
            for c, m in list(merged.items()):
                if m == drop:
                    merged[c] = keep
            merged[drop] = keep

    def write(self, out_dir):
        """Writes the binaries; returns maps.inc lines with the tileset and tile headers."""
        name = self.name
        mc_count = len({e & 0x3ff for (key, _) in self.meta_b if key[0] == 'mc'
                        for e in self.mc_meta[key[1] * 4:key[1] * 4 + 4]})
        kept, final = self.reduce(len(FREE_TILES) - mc_count)
        merged, sets, (pals, where) = self.quantize(kept)
        pal_lists = [sorted(p) for p in pals]
        chars = [None] * 1024
        chars[0] = chars[512] = bytes(32)
        free = list(FREE_TILES)
        char_of = {}
        for k, t in enumerate(kept):
            p = where[k]
            cols = pal_lists[p]
            pix = tuple(tuple(0 if c is None else cols.index(merged.get(c, c)) + 1 for c in r) for r in t)
            idx = free.pop(0)
            chars[idx] = gba_tile(pix)
            char_of[t] = idx | (GBA_ROWS[p] << 12)
        entry_of = {t: char_of[k] | bits for t, (k, bits) in final.items()}
        mc_tiles = {}

        def mc_entry(e):
            src = e & 0x3ff
            if src not in mc_tiles:
                idx = free.pop(0)
                chars[idx] = self.mc_chars[src * 32:src * 32 + 32]
                mc_tiles[src] = idx
            return (e & ~0x3ff) | mc_tiles[src]

        meta_b = sorted(self.meta_b.items(), key=lambda kv: kv[1])
        tiles_b, types_b = [], []
        for (key, ty), _ in meta_b:
            if key[0] == 'mc':
                tiles_b += [mc_entry(e) for e in self.mc_meta[key[1] * 4:key[1] * 4 + 4]]
            else:
                tiles_b += [entry_of[t] for t in key]
            types_b.append(ty)
        meta_t = sorted(self.meta_t.items(), key=lambda kv: kv[1])
        tiles_t = [0] * (4 * len(meta_t))
        types_t = [ty for ty, _ in meta_t]
        pal = []
        for cols in pal_lists:
            row = [(0, 0, 0)] + cols + [(0, 0, 0)] * (15 - len(cols))
            pal += [(r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) for (r, g, b) in row]
        chars = b''.join(c or bytes(32) for c in chars)
        files = {
            'chars': chars,
            'pal': struct.pack('<%dH' % len(pal), *pal),
            'tiles_bottom': struct.pack('<%dH' % len(tiles_b), *tiles_b),
            'tiles_top': struct.pack('<%dH' % len(tiles_t), *tiles_t),
            'types_bottom': struct.pack('<%dH' % len(types_b), *types_b),
            'types_top': struct.pack('<%dH' % len(types_t), *types_t),
        }
        lines = []
        for k, data in files.items():
            open(os.path.join(out_dir, '%s_%s.bin' % (name, k)), 'wb').write(data)
            lines.append('\t.align 2\ng%sTiles_%s::\n\t.incbin "custom/gen/%s_%s.bin"' % (name, k, name, k))
        lab = lambda k: '(g%sTiles_%s - gMapData)' % (name, k)
        lines += [
            '\t.align 2',
            'gAreaTileSet_%s_0::' % name,
            # the Fortress of Winds palettes first: they mark the rows as used
            '\ttileSet_palette_set 37',
            '\ttileSet_tiles %s, 0x6000000, 0x%x, 0' % (lab('chars'), len(chars)),
            '\tmap_data %s, gPaletteBuffer+0x%x, 0x%x, 0, 1' % (lab('pal'), GBA_ROWS[0] * 32, len(pal) * 2),
            'gAreaTileSets_%s::' % name,
            '\t.4byte gAreaTileSet_%s_0' % name,
            'gAreaTiles_%s::' % name,
            '\ttiles_bottom %s, 0x%x, 0' % (lab('tiles_bottom'), len(tiles_b) * 2),
            '\ttiles_top %s, 0x%x, 0' % (lab('tiles_top'), len(tiles_t) * 2),
            '\ttile_types_bottom %s, 0x%x, 0' % (lab('types_bottom'), len(types_b) * 2),
            '\ttile_types_top %s, 0x%x, 0, 1' % (lab('types_top'), len(types_t) * 2),
        ]
        print('%s tileset: %d tiles drawn as %d characters, %d bottom metatiles, %d palette rows, %d colors merged' % (
            name, len(self.tiles), 1024 - len(free), len(meta_b), len(pals), len(merged)))
        return lines
