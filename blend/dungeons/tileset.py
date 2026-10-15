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
# A Link to the Past's crystal switch pegs (0x66 orange, 0x67 blue)
PEG_ATTRS = (0x66, 0x67)
# the color of a drained basin's floor
BASIN = (58, 70, 84)
# how far from where Link can walk a dungeon with `trim` still draws A Link to the Past's art
TRIM_REACH = 3
# how much more a change in a tile's overall color counts than the same change in a
# few of its pixels, when tiles are merged to fit the character budget
MEAN_WEIGHT = int(os.environ.get('BLEND_MEAN_WEIGHT', '6'))


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


def wall_doors(room_id, lower=False):
    """(x, y, side) in 8px tiles of the doors A Link to the Past puts in a room's walls,
    on the layer the room is planned from (see build.read_room)."""
    data = open(os.path.join(A.ROOMS, '%03x.bin' % room_id), 'rb').read()
    o = 0x2000 + 0x4000 + 5 + 12
    types = struct.unpack('<16H', data[o:o + 32])
    addrs = struct.unpack('<16H', data[o + 32:o + 64])
    dirs = struct.unpack('<16H', data[o + 64:o + 96])
    out = []
    for t, a, d in zip(types, addrs, dirs):
        if t == 0 and a == 0:
            continue
        word = a >> 1
        if (word >= 0x1000) != lower:
            continue
        word &= 0xfff
        out.append((word % 64, word // 64, 'nswe'[d & 3]))
    return out


# how far apart (RGB) two neighboring 8px tiles' average colors are before a floor
# counts as two floors
FLOOR_SPLIT = 56
# a stretch of patterned floor this small (8px tiles) inside another floor takes its color
SMALL_FLOOR = 24


def split_by_color(comp, img, empty):
    """Splits one stretch of patterned floor (8px tile cells) into the floors it holds:
    neighbors stay together while their average colors are close. Groups too small to
    be a floor (a mark, a seam) join the group they touch most."""
    means = {}
    for y, x in comp:
        e = ~empty[y * 8:y * 8 + 8, x * 8:x * 8 + 8]
        means[(y, x)] = img[y * 8:y * 8 + 8, x * 8:x * 8 + 8][e].mean(axis=0) if e.any() else None
    cells = set(comp)
    group_of, groups = {}, []
    for start in comp:
        if start in group_of:
            continue
        g, stack = [], [start]
        group_of[start] = len(groups)
        while stack:
            p = stack.pop()
            g.append(p)
            for q in ((p[0] + 1, p[1]), (p[0] - 1, p[1]), (p[0], p[1] + 1), (p[0], p[1] - 1)):
                if q in cells and q not in group_of:
                    a, b = means[p], means[q]
                    if a is None or b is None or np.sqrt(((a - b) ** 2).sum()) < FLOOR_SPLIT:
                        group_of[q] = group_of[start]
                        stack.append(q)
        groups.append(g)
    if len(groups) == 1:
        return groups
    big = [i for i, g in enumerate(groups) if len(g) >= 8]
    if not big:
        return [comp]
    out = {i: list(groups[i]) for i in big}
    for i, g in enumerate(groups):
        if i in out:
            continue
        touch = {}
        for p in g:
            for q in ((p[0] + 1, p[1]), (p[0] - 1, p[1]), (p[0], p[1] + 1), (p[0], p[1] - 1)):
                j = group_of.get(q)
                if j in out:
                    touch[j] = touch.get(j, 0) + 1
        j = max(touch, key=touch.get) if touch else max(out, key=lambda k: len(out[k]))
        out[j] += g
    return list(out.values())


class Blend:
    def __init__(self, name, tileset_area='043', trim=False):
        self.name = name
        self.trim = trim
        self.types_b = mctiles.tile_types(tileset_area, 'bottom')
        self.types_t = mctiles.tile_types(tileset_area, 'top')
        self.mc_meta = mcrender.metatiles(tileset_area, 'bottom')
        self.mc_chars = mcrender.gfx(tileset_area)
        self.tiles = {}           # 8px tile (RGB pixels, None where empty) -> how often it is used
        self.meta_b, self.meta_t = {}, {}   # key -> metatile index
        self.colors = None
        self.tones = None
        self.switch_rooms = set()  # rooms whose crystal switches work (see pegs)

    # rooms

    def rgb(self, c):
        return self.tones[blendstyle.blend_color(alttpgfx.bgr555(self.colors[c]))]

    def compose(self, gfx):
        """The room as one picture, layered as the SNES shows it, in blended, banded colors."""
        img = np.zeros((512, 512, 3))
        empty = np.ones((512, 512), dtype=bool)
        lut = np.array([self.rgb(c) if c else (0, 0, 0) for c in range(256)], dtype=float)
        for i in range(4096):
            pix = np.array(gfx.composite(i, unmask=not gfx.is_pit(i)))
            tx, ty = (i % 64) * 8, (i // 64) * 8
            img[ty:ty + 8, tx:tx + 8] = lut[pix]
            empty[ty:ty + 8, tx:tx + 8] = pix == 0
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
        stoned = self.restone(gfx, plan, img, empty)
        self.center_doorways(plan, (img, empty, stoned))
        # floor Link can't reach that runs out to the room's edge is the space around
        # the room, however A Link to the Past patterned it
        def floorlike(i):
            if plan.cells[i] in PASSABLE:
                return False
            cell = (slice((i // 32) * 16, (i // 32) * 16 + 16), slice((i % 32) * 16, (i % 32) * 16 + 16))
            return alttp[i] == A.FLOOR or stoned[cell].mean() > 0.5
        outside = set()
        stack = [i for i in range(1024) if (i % 32 in (0, 31) or i // 32 in (0, 31)) and floorlike(i)]
        while stack:
            i = stack.pop()
            if i in outside:
                continue
            outside.add(i)
            cx, cy = i % 32, i // 32
            for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                j = ny * 32 + nx
                if 0 <= nx < 32 and 0 <= ny < 32 and j not in outside and floorlike(j):
                    stack.append(j)
        if self.trim:
            outside |= self.unreached(plan)
        for i in outside:
            dst = (slice((i // 32) * 16, (i // 32) * 16 + 16), slice((i % 32) * 16, (i % 32) * 16 + 16))
            img[dst] = VOID
            empty[dst] = False
            stoned[dst] = False

        # floor Link walks on that A Link to the Past leaves bare (the Swamp Palace's drained
        # basins, black until the water comes in) would read as a pit: lay it in dark, wet slate
        # (tile by tile, wherever A Link to the Past's floor attribute says Link can walk)
        yy, xx = np.mgrid[0:512, 0:512]
        slab = np.empty((512, 512, 3))
        slab[:] = celshade.snap(np.array(BASIN, dtype=float))
        slab[(xx % 16 == 0) | (yy % 16 == 0)] = celshade.snap(np.array(BASIN) + (255 - np.array(BASIN)) * 0.12)
        slab[(xx % 16 == 15) | (yy % 16 == 15)] = celshade.snap(np.array(BASIN) * 0.8)
        for t in range(4096):
            tx, ty = t % 64, t // 64
            i = (ty // 2) * 32 + tx // 2
            tile = (slice(ty * 8, ty * 8 + 8), slice(tx * 8, tx * 8 + 8))
            if i not in outside and A.attr_class(plan.room.attr[t]) == A.FLOOR and \
                    plan.cells[i] in PASSABLE + (A.SOLID,) and empty[tile].mean() > 0.5:
                img[tile] = slab[tile]
                empty[tile] = False
                stoned[tile] = True

        # where the plan opens a way through A Link to the Past's art, draw the nearest floor
        floor = [(i % 32, i // 32) for i in range(1024) if alttp[i] == A.FLOOR and plan.cells[i] == A.FLOOR
                 and stoned[(i // 32) * 16:(i // 32) * 16 + 16, (i % 32) * 16:(i % 32) * 16 + 16].all()] or \
                [(i % 32, i // 32) for i in range(1024) if alttp[i] == A.FLOOR and plan.cells[i] == A.FLOOR]
        cls = np.array([[A.attr_class(plan.room.attr[y * 64 + x]) for x in range(64)] for y in range(64)])
        cls = cls.repeat(8, axis=0).repeat(8, axis=1)
        carved = []
        # crystal switch pegs, which only the rooms with working switches keep: elsewhere
        # they are the plain floor Link walks over
        peg_cells = {(t // 64 // 2) * 32 + (t % 64) // 2 for t in range(4096) if plan.room.attr[t] in PEG_ATTRS}
        # the mat an A Link to the Past chest stands on (a framed square of other tiles):
        # its chest is a Minish Cap one on the 16px grid now, so the mat would be left
        # around nothing, half a cell off; the floor runs on under the chest instead
        chest_mats = set()
        for t in range(4096):
            if A.attr_class(plan.room.attr[t]) == A.CHEST:
                tx, ty = t % 64, t // 64
                for my in range(max(0, ty - 3), min(64, ty + 4)):
                    for mx in range(max(0, tx - 3), min(64, tx + 4)):
                        chest_mats.add((my // 2) * 32 + mx // 2)
        for i in range(1024):
            cell = img[(i // 32) * 16:(i // 32) * 16 + 16, (i % 32) * 16:(i % 32) * 16 + 16]
            dark = np.all(cell == VOID, axis=-1).mean() > 0.5
            pegs = i in peg_cells and plan.id not in self.switch_rooms
            mat = i in chest_mats and not stoned[(i // 32) * 16:(i // 32) * 16 + 16, (i % 32) * 16:(i % 32) * 16 + 16].mean() > 0.5
            if plan.cells[i] in PASSABLE and (alttp[i] in REDRAW_AS_FLOOR or dark or pegs or mat) and floor:
                cx, cy = i % 32, i // 32
                sx, sy = min(floor, key=lambda p: abs(p[0] - cx) + abs(p[1] - cy))
                dst = (slice(cy * 16, cy * 16 + 16), slice(cx * 16, cx * 16 + 16))
                src = (slice(sy * 16, sy * 16 + 16), slice(sx * 16, sx * 16 + 16))
                img[dst] = img[src]
                empty[dst] = empty[src]
                stoned[dst] = True
                cls[dst] = A.FLOOR
                carved.append((cx, cy))
        self._dbg = (carved, outside)
        # a passage cut through a wall or across the dark gets edges, so it reads as a
        # walkway rather than a smear of floor
        for cx, cy in carved:
            x0, y0 = cx * 16, cy * 16
            for (nx, ny), side in (((cx - 1, cy), 'l'), ((cx + 1, cy), 'r'), ((cx, cy - 1), 't'), ((cx, cy + 1), 'b')):
                if 0 <= nx < 32 and 0 <= ny < 32 and plan.cells[ny * 32 + nx] in PASSABLE:
                    continue
                # only where it meets the dark: floor beside an invisible wall stays plain
                if 0 <= nx < 32 and 0 <= ny < 32:
                    ncell = img[ny * 16:ny * 16 + 16, nx * 16:nx * 16 + 16]
                    if np.all(ncell == VOID, axis=-1).mean() <= 0.5:
                        continue
                edge = {'l': (slice(y0, y0 + 16), slice(x0, x0 + 2)), 'r': (slice(y0, y0 + 16), slice(x0 + 14, x0 + 16)),
                        't': (slice(y0, y0 + 2), slice(x0, x0 + 16)), 'b': (slice(y0 + 14, y0 + 16), slice(x0, x0 + 16))}[side]
                img[edge] = celshade.OUTLINE
        # a step back up out of a room Link dropped into (A Link to the Past has none; Link
        # leaves Mothula's lair by the crystal): four stone treads in the floor's own color
        for cx, cy in plan.steps_up:
            x0, y0 = cx * 16, cy * 16
            base = img[y0:y0 + 16, x0:x0 + 16].reshape(-1, 3).mean(axis=0)
            for k in range(4):
                ty = y0 + k * 4
                img[ty, x0:x0 + 16] = celshade.snap(base + (255 - base) * (0.35 - 0.08 * k))
                img[ty + 1:ty + 3, x0:x0 + 16] = celshade.snap(base * (1.0 - 0.15 * k))
                img[ty + 3, x0:x0 + 16] = celshade.snap(base * (0.55 - 0.08 * k))
            img[y0:y0 + 16, x0] = celshade.OUTLINE
            img[y0:y0 + 16, x0 + 15] = celshade.OUTLINE
            empty[y0:y0 + 16, x0:x0 + 16] = False
            stoned[y0:y0 + 16, x0:x0 + 16] = True
        # shadows only fall where Link can go: A Link to the Past leaves unseen floor and
        # invisible walls around its rooms
        walk = np.array([[plan.cells[(y // 16) * 32 + x // 16] in PASSABLE for x in range(512)] for y in range(512)])
        receive = np.isin(cls, RECEIVES) & walk
        # floor never casts, even where A Link to the Past has an invisible wall on it
        cast = np.isin(cls, CASTS) & ~walk & ~stoned
        # a ring of spikes (Mothula's lair), wall here: the cel-shading's outlines and rim
        # light turn its rows of studs into thin black-and-white columns, so it is shaded
        # softly instead, a calm studded border
        soft = None
        if plan.id in A.SPIKE_RING_ROOMS:
            spikes = np.isin(np.frombuffer(bytes(plan.room.attr), dtype=np.uint8), A.SPIKE_ATTRS).reshape(64, 64)
            soft = spikes.repeat(8, axis=0).repeat(8, axis=1) & ~empty
            mean = img[soft].mean(axis=0)
            img[soft] = celshade.snap(mean + (img[soft] - mean) * 0.6)
        img = celshade.shade(img, empty, receive, cast, soft)

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

    def center_doorways(self, plan, arrays):
        """A Link to the Past's doors in a north or south wall are 16px openings 8px off
        Minish Cap's 16px grid, so the one-cell way through the wall the plan cuts would
        run beside the opening, half through the door frame. Moves each such door's art
        (opening, frames and the statues beside it) 8px left onto the way through; the wall beyond closes the gap."""
        attr = plan.room.attr
        door_cls = (A.DOOR, A.LOCKED, A.SHUTTER, A.ENTRANCE)
        for x, y, side in wall_doors(plan.id, plan.lower):
            if side not in ('n', 's') or x % 2 or not 2 <= x <= 58:
                continue
            col = x + 1
            rows = [r for r in range(max(0, y - 6), min(64, y + 7)) if A.attr_class(attr[r * 64 + col]) in door_cls]
            if not rows:
                continue
            r0, r1 = min(rows), max(rows) + 1
            cx = x // 2
            way = [r for r in range(r0, r1) if plan.cells[(r // 2) * 32 + cx] in PASSABLE]
            beside = [r for r in range(r0, r1) if plan.cells[(r // 2) * 32 + cx + 1] in PASSABLE]
            if not way or beside:
                continue
            # the door's art reaches a tile past its attributes; floor slabs stay put
            y0, y1, x0, x1 = max(0, r0 - 1) * 8, min(64, r1 + 1) * 8, max(1, x - 4) * 8, min(63, x + 8) * 8
            img, empty, stoned = arrays
            keep = stoned[y0:y1, x0 - 8:x1].copy()
            keep[:, :-8] &= stoned[y0:y1, x0:x1]
            keep[:, -8:] &= stoned[y0:y1, x1:x1 + 8]
            for a in arrays:
                old = a[y0:y1, x0 - 8:x1].copy()
                a[y0:y1, x0 - 8:x1 - 8] = a[y0:y1, x0:x1].copy()
                a[y0:y1, x1 - 8:x1] = a[y0:y1, x1:x1 + 8].copy()
                a[y0:y1, x0 - 8:x1][keep] = old[keep]

    def unreached(self, plan):
        """Cells more than TRIM_REACH cells from anywhere Link can walk: the parts of A Link
        to the Past's rooms the plan walls off (other quarters of the room, closed-off
        floor), which would otherwise show as rooms Link can't enter. Pits and water stay."""
        near = np.array([c in PASSABLE for c in plan.cells]).reshape(32, 32)
        for _ in range(TRIM_REACH):
            grown = near.copy()
            grown[1:, :] |= near[:-1, :]
            grown[:-1, :] |= near[1:, :]
            grown[:, 1:] |= near[:, :-1]
            grown[:, :-1] |= near[:, 1:]
            grown[1:, 1:] |= near[:-1, :-1]
            grown[:-1, :-1] |= near[1:, 1:]
            grown[1:, :-1] |= near[:-1, 1:]
            grown[:-1, 1:] |= near[1:, :-1]
            near = grown
        # pits and water stay where they reach (pit to pit) within that distance: a chasm
        # beside the floor keeps its whole extent, a pit off in an unreached quarter goes
        wet = [plan.room.cells[i] in (A.PIT, A.WATER) for i in range(1024)]
        keep = {i for i in range(1024) if wet[i] and near.flat[i]}
        stack = list(keep)
        while stack:
            i = stack.pop()
            x, y = i % 32, i // 32
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                j = ny * 32 + nx
                if 0 <= nx < 32 and 0 <= ny < 32 and wet[j] and j not in keep:
                    keep.add(j)
                    stack.append(j)
        return {i for i in range(1024) if not near.flat[i] and i not in keep}

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
        empty_hi = ~np.array([gfx.upper_in_front(i) for i in range(4096)]).reshape(64, 64)
        # floor on the layer the room was planned from, and on the other one where that
        # holds anything (an unused layer is all zeros, which read as floor, walls included)
        # (a maze of thin walls is planned from its walls alone)
        lower_used = len(set(plan.room.lower)) > 1 and plan.id not in A.THIN_WALL_ROOMS
        floor = np.array([A.attr_class(a) == A.FLOOR or (lower_used and A.attr_class(b) == A.FLOOR)
                          for a, b in zip(plan.room.attr, plan.room.lower)]).reshape(64, 64)
        # and the patterned fill around the rooms, where Link can't go (not in a maze of
        # thin walls, whose wall tiles repeat like a floor pattern)
        # Only a cell or more away from where Link walks: the fill lies beyond the walls,
        # while a row of pillars standing on the floor (Skull Woods's room 0x57) repeats
        # like a pattern too and must keep its art
        if plan.id not in A.THIN_WALL_ROOMS:
            passable = np.array([c in PASSABLE for c in plan.cells]).reshape(32, 32)
            near = passable.copy()
            near[1:, :] |= passable[:-1, :]
            near[:-1, :] |= passable[1:, :]
            near[:, 1:] |= passable[:, :-1]
            near[:, :-1] |= passable[:, 1:]
            floor |= ~near.repeat(2, axis=0).repeat(2, axis=1)
        if plan.id in A.SPIKE_RING_ROOMS:
            # a ring of spikes repeats like a floor pattern too; its spikes stay spikes
            floor &= ~np.isin(np.frombuffer(bytes(plan.room.attr), dtype=np.uint8), A.SPIKE_ATTRS).reshape(64, 64)
        on_hi = pattern(gfx.bg1) & ~empty_hi & floor
        on_lo = pattern(gfx.bg2) & empty_hi & floor
        walk = np.array([plan.cells[(y // 2) * 32 + x // 2] in PASSABLE for y in range(64) for x in range(64)]).reshape(64, 64)
        yy, xx = np.mgrid[0:512, 0:512]
        stoned = np.zeros((512, 512), dtype=bool)
        for layer, tiles in ((gfx.bg2, on_lo), (gfx.bg1, on_hi)):
            words = np.array(layer).reshape(64, 64) & 0xffff
            comps = []
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
                    comps.append(comp)
            # the space around the room is the pattern that reaches the room's edge where
            # Link can't walk; wherever else that pattern shows (holes in a floor), it is too
            void_words = set()
            for comp in comps:
                if sum(walk[y, x] for y, x in comp) < 0.15 * len(comp) and \
                        any(y in (0, 63) or x in (0, 63) for y, x in comp):
                    void_words |= {int(words[y, x]) for y, x in comp}
            # the same pattern outside the floor attributes (under doorways and ledges)
            front = ~empty_hi
            if void_words:
                rest = np.isin(words, list(void_words)) & (front if layer is gfx.bg1 else empty_hi) & ~tiles
                mask = rest.repeat(8, axis=0).repeat(8, axis=1) & ~empty
                img[mask] = VOID
            # big floors first: a small patch of patterned floor inside one (a big chest's
            # mat, a square of tiles around a floor switch) takes the floor around it
            for comp in sorted(comps, key=len, reverse=True):
                part = np.zeros((64, 64), dtype=bool)
                for y, x in comp:
                    part[y, x] = True
                mask = part.repeat(8, axis=0).repeat(8, axis=1) & ~empty
                if not mask.any():
                    continue
                walkable = sum(walk[y, x] for y, x in comp) / len(comp)
                if walkable < 0.15 and {int(words[y, x]) for y, x in comp} <= void_words:
                    # plain darkness, as around Minish Cap's rooms
                    img[mask] = VOID
                    continue
                if len(comp) < 4:
                    continue
                # each stretch of floor gets its own color: one pattern can lay two floors
                # (the Palace of Darkness's turtle hall has green flagstones above its blue
                # tiles), so the stretch is split where the floor's own color changes
                for group in split_by_color(comp, img, empty):
                    part = np.zeros((64, 64), dtype=bool)
                    for y, x in group:
                        part[y, x] = True
                    gmask = part.repeat(8, axis=0).repeat(8, axis=1) & ~empty
                    if not gmask.any():
                        continue
                    base = celshade.snap(np.median(img[gmask], axis=0))
                    if len(group) < SMALL_FLOOR:
                        ys = [y for y, x in group]
                        xs = [x for y, x in group]
                        box = (slice(max(0, min(ys) - 2) * 8, min(64, max(ys) + 3) * 8),
                               slice(max(0, min(xs) - 2) * 8, min(64, max(xs) + 3) * 8))
                        around = stoned[box] & ~gmask[box]
                        if around.sum() >= 64:
                            base = celshade.snap(np.median(img[box][around], axis=0))
                    stone = np.empty((512, 512, 3))
                    stone[:] = base
                    stone[((xx // 16 + yy // 16) % 2) == 1] = celshade.snap(base * 0.95)
                    stone[(xx % 16 == 0) | (yy % 16 == 0)] = celshade.snap(base + (255 - base) * 0.16)
                    stone[(xx % 16 == 15) | (yy % 16 == 15)] = celshade.snap(base * 0.82)
                    img[gmask] = stone[gmask]
                    stoned |= gmask
        if plan.id in A.THIN_WALL_ROOMS:
            # around a maze, the pattern that fills the room's border is the space outside
            words = np.array(gfx.bg2).reshape(64, 64) & 0xffff
            ring = set(words[0]) | set(words[-1]) | set(words[:, 0]) | set(words[:, -1])
            mask = np.isin(words, list(ring)).repeat(8, axis=0).repeat(8, axis=1)
            img[mask] = VOID
            empty[mask] = False
        return stoned

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
            # a whole tile turning lighter or darker shows far more than a few pixels
            # changing: a floor slab swapped for its shaded twin reads as a checker or a
            # patch across the room, so a change in a tile's overall color costs extra
            mean = arr.reshape(n, 64, 3).mean(axis=1)
            for i in range(0, n, 256):
                dist[i:i + 256] += MEAN_WEIGHT * 64 * ((mean[i:i + 256, None, :] - mean[None, :, :]) ** 2).sum(-1)
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

    def quantize(self, kept, usage=None):
        """Fits the colors into the palette rows, merging tones until they fit: the pair
        whose merge changes the fewest pixels the least goes first, and the rarer color
        takes the commoner one, so a floor keeps its own tones (merging toward the darker
        color turned the half of a floor's slabs into a checker). usage: {color: pixels}."""
        merged = {}
        use = dict(usage or {})
        while True:
            sets = [frozenset(merged.get(c, c) for r in t for c in r if c is not None) for t in kept]
            packed = pack_palettes(sets, len(GBA_ROWS))
            if packed:
                return merged, sets, packed
            colors = sorted({c for s in sets for c in s})
            best = None
            for i, a in enumerate(colors):
                for b in colors[i + 1:]:
                    d = color_distance(a, b) * (min(use.get(a, 1), use.get(b, 1)) if usage else 1)
                    if best is None or d < best[0]:
                        best = (d, a, b)
            _, a, b = best
            if usage:
                keep, drop = (a, b) if use.get(a, 1) >= use.get(b, 1) else (b, a)
            else:
                keep, drop = (a, b) if sum(a) < sum(b) else (b, a)
            for c, m in list(merged.items()):
                if m == drop:
                    merged[c] = keep
            merged[drop] = keep
            use[keep] = use.get(keep, 1) + use.pop(drop, 1)

    def write(self, out_dir):
        """Writes the binaries; returns maps.inc lines with the tileset and tile headers."""
        name = self.name
        mc_count = len({e & 0x3ff for (key, _) in self.meta_b if key[0] == 'mc'
                        for e in self.mc_meta[key[1] * 4:key[1] * 4 + 4]})
        kept, final = self.reduce(len(FREE_TILES) - mc_count)
        # how many pixels of the rooms show each color, after the tile merges
        usage = {}
        for t, n in self.tiles.items():
            k, _ = final[t]
            for r in k:
                for c in r:
                    if c is not None:
                        usage[c] = usage.get(c, 0) + n
        merged, sets, (pals, where) = self.quantize(kept, usage)
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
