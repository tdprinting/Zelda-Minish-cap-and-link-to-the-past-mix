"""
The blended dungeon tileset: each room is drawn with A Link to the Past's own
architecture (its two background layers flattened into one), re-lit in Minish
Cap's colors by blendstyle, with Minish Cap's chests on top. Collision still
comes from the Minish Cap tile types the autotiler picked for the room plan, so
the rooms play exactly as they did with the Minish Cap tiles.

Writes, for one dungeon, a 32KB character block, its palettes and the metatile
and tile type tables, plus the headers that load them (into maps.inc).
"""
import os
import struct

import alttprooms as A
import alttpgfx
import blendstyle
import mcrender
import mctiles

# tiles 0 and 512 stay blank: 0 for the bottom layer, 512 is tile 0 of the top
# layer (its characters start 16KB in), which is left empty
FREE_TILES = [i for i in range(1, 1024) if i != 512]
# A Link to the Past background palette rows go to these Game Boy Advance rows
GBA_ROWS = list(range(2, 15))
MAX_METATILES = 2048

# A Link to the Past classes that are drawn as plain floor wherever the plan lets
# Link walk: doors the Minish Cap door objects now draw, and blocks and pots the
# plan dropped, and A Link to the Past chests (Minish Cap ones replace them)
REDRAW_AS_FLOOR = (A.LOCKED, A.SHUTTER, A.ENTRANCE, A.BLOCK, A.POT, A.CHEST, A.BIG_CHEST, A.SOLID, A.TORCH, A.PIT, A.WATER)
PASSABLE = (A.FLOOR, '+')


def gba_tile(pix):
    out = bytearray(32)
    for y in range(8):
        for x in range(0, 8, 2):
            out[y * 4 + x // 2] = (pix[y][x] & 15) | ((pix[y][x + 1] & 15) << 4)
    return bytes(out)


def flips(pix):
    h = tuple(r[::-1] for r in pix)
    return [(pix, 0), (h, 0x400), (pix[::-1], 0x800), (h[::-1], 0xc00)]


class Blend:
    def __init__(self, name, tileset_area='043'):
        self.name = name
        self.types_b = mctiles.tile_types(tileset_area, 'bottom')
        self.types_t = mctiles.tile_types(tileset_area, 'top')
        self.mc_meta = mcrender.metatiles(tileset_area, 'bottom')
        self.mc_chars = mcrender.gfx(tileset_area)
        self.chars = [None] * 1024
        self.chars[0] = self.chars[512] = bytes(32)
        self.free = list(FREE_TILES)
        self.tile_index = {}      # (palette row, pixels) -> entry without palette bits
        self.meta_b, self.meta_t = {}, {}   # key -> metatile index
        self.rows = {}            # A Link to the Past row -> Game Boy Advance row
        self.colors = None
        self.mc_tiles = {}        # Minish Cap character -> our character

    # characters

    def add_tile(self, pix):
        pix = tuple(tuple(r) for r in pix)
        hit = self.tile_index.get(pix)
        if hit is not None:
            return hit
        if not self.free:
            raise RuntimeError('%s: more than 1022 distinct 8px tiles' % self.name)
        idx = self.free.pop(0)
        self.chars[idx] = gba_tile(pix)
        for variant, bits in flips(pix):
            self.tile_index.setdefault(variant, idx | bits)
        return idx

    def mc_entry(self, entry):
        """A Minish Cap tilemap entry with its character copied into our block."""
        src = entry & 0x3ff
        if src not in self.mc_tiles:
            idx = self.free.pop(0)
            self.chars[idx] = self.mc_chars[src * 32:src * 32 + 32]
            self.mc_tiles[src] = idx
        return (entry & ~0x3ff) | self.mc_tiles[src]

    # colors

    def row(self, alttp_row):
        if alttp_row not in self.rows:
            if len(self.rows) == len(GBA_ROWS):
                raise RuntimeError('%s: too many palette rows' % self.name)
            self.rows[alttp_row] = GBA_ROWS[len(self.rows)]
        return self.rows[alttp_row]

    def rgb(self, c):
        return blendstyle.blend_color(alttpgfx.bgr555(self.colors[c]))

    def nearest(self, c, row):
        r, g, b = self.rgb(c)
        best = min(range(1, 16), key=lambda k: sum((p - q) ** 2 for p, q in zip(self.rgb(row * 16 + k), (r, g, b))))
        return best

    def flatten(self, gfx, i):
        """One 8px tile of the room, upper layer over lower, as (A Link to the Past row, 8x8 color numbers)."""
        lo, hi = gfx.tile(gfx.bg2[i]), gfx.tile(gfx.bg1[i])
        pix = [[hi[y][x] or lo[y][x] for x in range(8)] for y in range(8)]
        rows = {}
        for r in pix:
            for c in r:
                if c:
                    rows[c >> 4] = rows.get(c >> 4, 0) + 1
        if not rows:
            return None, [[0] * 8 for _ in range(8)]
        main = max(rows, key=rows.get)
        out = []
        for r in pix:
            out.append([0 if not c else (c & 15) if c >> 4 == main else self.nearest(c, main) for c in r])
        return main, out

    def entry(self, gfx, i):
        row, pix = self.flatten(gfx, i)
        e = self.add_tile(pix)
        return e if row is None else e | (self.row(row) << 12)

    # metatiles

    def metatile(self, table, key):
        if key not in table:
            if len(table) >= MAX_METATILES:
                raise RuntimeError('%s: more than %d metatiles' % (self.name, MAX_METATILES))
            table[key] = len(table)
        return table[key]

    def room(self, plan, bottom, top):
        """Redraws one room: bottom/top are the autotiler's Minish Cap metatiles,
        whose tile types are kept. Returns the room's new bottom and top cells."""
        gfx = alttpgfx.RoomGfx(plan.id)
        if self.colors is None:
            self.colors = gfx.colors
        elif gfx.colors[:128] != self.colors[:128]:
            print('%s: room %03x has other colors; using the first room\'s' % (self.name, plan.id))
        if not self.meta_t:
            # top layer: empty, with the tile types the autotiler chose
            self.metatile(self.meta_t, self.types_t[0])
        alttp = plan.room.cells
        floor = [(i % 32, i // 32) for i in range(1024) if alttp[i] == A.FLOOR and plan.cells[i] == A.FLOOR]
        new_b, new_t = [], []
        for i in range(1024):
            cx, cy = i % 32, i // 32
            b, t = bottom[i], top[i]
            if t >= 0x4000:
                new_t.append(t)
            else:
                new_t.append(self.metatile(self.meta_t, self.types_t[t]))
            if b >= 0x4000:
                new_b.append(b)
                continue
            tile_type = self.types_b[b]
            if tile_type in (0x73, 0x74):
                # Minish Cap chests, so they open the Minish Cap way
                entries = tuple(self.mc_entry(e) for e in self.mc_meta[b * 4:b * 4 + 4])
            else:
                sx, sy = cx, cy
                if plan.cells[i] in PASSABLE and alttp[i] in REDRAW_AS_FLOOR and floor:
                    sx, sy = min(floor, key=lambda p: abs(p[0] - cx) + abs(p[1] - cy))
                entries = tuple(self.entry(gfx, (sy * 2 + dy) * 64 + sx * 2 + dx) for dy in (0, 1) for dx in (0, 1))
            new_b.append(self.metatile(self.meta_b, (entries, tile_type)))
        # the open chest must exist for a chest to open into
        for b in range(len(self.types_b)):
            if self.types_b[b] == 0x74:
                self.metatile(self.meta_b, (tuple(self.mc_entry(e) for e in self.mc_meta[b * 4:b * 4 + 4]), 0x74))
                break
        return new_b, new_t

    # output

    def palette(self):
        out = []
        for alttp_row, gba_row in sorted(self.rows.items(), key=lambda kv: kv[1]):
            for k in range(16):
                r, g, b = self.rgb(alttp_row * 16 + k)
                out.append((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10))
        return out

    def write(self, out_dir):
        """Writes the binaries; returns maps.inc lines with the tileset and tile headers."""
        name = self.name
        chars = b''.join(c or bytes(32) for c in self.chars)
        pal = self.palette()
        meta_b = sorted(self.meta_b.items(), key=lambda kv: kv[1])
        meta_t = sorted(self.meta_t.items(), key=lambda kv: kv[1])
        tiles_b = [e for (entries, _), _ in meta_b for e in entries]
        types_b = [ty for (_, ty), _ in meta_b]
        tiles_t = [0] * (4 * len(meta_t))
        types_t = [ty for ty, _ in meta_t]
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
        first_row = min(self.rows.values())
        lines += [
            '\t.align 2',
            'gAreaTileSet_%s_0::' % name,
            # the Fortress of Winds palettes first: they mark the rows as used
            '\ttileSet_palette_set 37',
            '\ttileSet_tiles %s, 0x6000000, 0x%x, 0' % (lab('chars'), len(chars)),
            '\tmap_data %s, gPaletteBuffer+0x%x, 0x%x, 0, 1' % (lab('pal'), first_row * 32, len(pal) * 2),
            'gAreaTileSets_%s::' % name,
            '\t.4byte gAreaTileSet_%s_0' % name,
            'gAreaTiles_%s::' % name,
            '\ttiles_bottom %s, 0x%x, 0' % (lab('tiles_bottom'), len(tiles_b) * 2),
            '\ttiles_top %s, 0x%x, 0' % (lab('tiles_top'), len(tiles_t) * 2),
            '\ttile_types_bottom %s, 0x%x, 0' % (lab('types_bottom'), len(types_b) * 2),
            '\ttile_types_top %s, 0x%x, 0, 1' % (lab('types_top'), len(types_t) * 2),
        ]
        print('%s tileset: %d characters, %d bottom metatiles, %d palette rows' % (
            name, 1024 - len(self.free), len(meta_b), len(self.rows)))
        return lines
