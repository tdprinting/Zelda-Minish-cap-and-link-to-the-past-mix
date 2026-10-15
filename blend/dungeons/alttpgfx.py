"""
A Link to the Past room graphics as the game shows them: the background tiles and
colors dumped by `dumprooms gfx`, and the room's two tilemaps.
"""
import os
import struct

from PIL import Image

import alttprooms as A

GFX = os.environ.get('ALTTP_GFX', os.path.join(os.path.dirname(A.ROOMS.rstrip('/')), 'gfx'))


def snes_tile(chars, index):
    b = chars[index * 32:index * 32 + 32]
    rows = []
    for y in range(8):
        p0, p1, p2, p3 = b[y * 2], b[y * 2 + 1], b[16 + y * 2], b[16 + y * 2 + 1]
        rows.append([((p0 >> (7 - x)) & 1) | (((p1 >> (7 - x)) & 1) << 1) |
                     (((p2 >> (7 - x)) & 1) << 2) | (((p3 >> (7 - x)) & 1) << 3) for x in range(8)])
    return rows


def bgr555(c):
    return ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)


YAML = os.environ.get('ALTTP_YAML', '/home/claude/z3/assets/dungeon')
# room header layer settings where the second layer sits behind the main one
BACKDROP_MODES = ('None', 'Parallaxing', 'Translucent', 'Transparent', 'Addition', 'Off')


# rooms set to "Normal" whose second layer is still a backdrop: the Desert Palace's
# courtyard fills it with a checkered floor that would cover the room
BACKDROP_ROOMS = {0x53}

# rooms whose first layer only holds a cover over part of the room, which the game
# lifts as Link comes near: the Palace of Darkness's room 0x1B ("On top") hides its
# staircase corner under one. The room is all on the second layer; the cover is left out
COVER_ROOMS = {0x1b}


def layer_mode(room_id):
    """The room header's second-layer setting ("Normal", "None", "Parallaxing"...)."""
    try:
        for line in open(os.path.join(YAML, 'dungeon-%d.yaml' % room_id)):
            if line.strip().startswith('bg2:'):
                return line.split(':', 1)[1].strip()
    except OSError:
        pass
    return 'Normal'


class RoomGfx:
    def __init__(self, room_id):
        data = open(os.path.join(GFX, '%03x.gfx' % room_id), 'rb').read()
        self.chars = data[:0x4000]
        self.colors = list(struct.unpack('<256H', data[0x4000:0x4200]))
        raw = open(os.path.join(A.ROOMS, '%03x.bin' % room_id), 'rb').read()
        self.attr, self.lower = raw[:0x1000], raw[0x1000:0x2000]
        self.bg2 = list(struct.unpack('<4096H', raw[0x2000:0x4000]))
        self.bg1 = list(struct.unpack('<4096H', raw[0x4000:0x6000]))
        if room_id in COVER_ROOMS:
            self.bg1 = [0x3ff] * 4096  # a blank tile
        self.layer_mode = 'None' if room_id in BACKDROP_ROOMS else layer_mode(room_id)
        self._cache = {}

    def tile(self, entry):
        """8x8 color indexes (0..255, 0 = clear) for one tilemap entry."""
        key = entry & 0xdfff
        if key not in self._cache:
            idx, pal = entry & 0x3ff, (entry >> 10) & 7
            pix = snes_tile(self.chars, idx) if idx < 512 else [[0] * 8 for _ in range(8)]
            if entry & 0x8000:
                pix = pix[::-1]
            if entry & 0x4000:
                pix = [r[::-1] for r in pix]
            self._cache[key] = [[(pal * 16 + c) if c else 0 for c in r] for r in pix]
        return self._cache[key]

    def composite(self, i, unmask=True):
        """Tile i of the room as the SNES shows it: front to back, upper layer tiles
        with the priority bit, then lower layer ones with it, then upper, then lower."""
        front, back = self.order(i)
        if unmask and any(any(r) for r in back):
            # black drawn over art on a walkable tile: A Link to the Past's darkness masks
            # (lights-out rooms), which the game lights up; show what's under them
            return [[back[y][x] if (front[y][x] and self.is_black(front[y][x]) and back[y][x])
                     else (front[y][x] or back[y][x]) for x in range(8)] for y in range(8)]
        return [[front[y][x] or back[y][x] for x in range(8)] for y in range(8)]

    def is_pit(self, i):
        return A.attr_class(self.attr[i]) in (A.PIT, A.WATER) or A.attr_class(self.lower[i]) in (A.PIT, A.WATER)

    def is_black(self, c):
        r, g, b = bgr555(self.colors[c])
        return r + g + b < 60

    def order(self, i):
        """Front and back tile. Where both layers are shown (header "Normal", "On top",
        "Dark") the SNES rule holds: BG1 is in front unless only BG2 has the priority
        bit, so a walkway on BG1 stays above the floor under it. Where the second layer
        is a backdrop (parallax, translucent or "None") it shows only through the gaps
        of the room's main layer, unless only it has the priority bit: the Desert
        Palace fills that whole layer with a floor pattern."""
        hi, lo = self.bg1[i], self.bg2[i]
        if self.layer_mode in BACKDROP_MODES:
            if (hi & 0x2000) and not (lo & 0x2000):
                return self.tile(hi), self.tile(lo)
            return self.tile(lo), self.tile(hi)
        if (lo & 0x2000) and not (hi & 0x2000):
            return self.tile(lo), self.tile(hi)
        return self.tile(hi), self.tile(lo)

    def upper_in_front(self, i):
        """Whether most of what shows in tile i is the upper layer."""
        hi, lo = self.tile(self.bg1[i]), self.tile(self.bg2[i])
        shown = self.composite(i, unmask=not self.is_pit(i))
        shown_hi = sum(1 for y in range(8) for x in range(8)
                       if shown[y][x] and shown[y][x] == hi[y][x] and shown[y][x] != lo[y][x])
        shown_lo = sum(1 for y in range(8) for x in range(8)
                       if shown[y][x] and shown[y][x] == lo[y][x] and shown[y][x] != hi[y][x])
        return shown_hi > shown_lo

    def image(self, layers=('bg2', 'bg1')):
        im = Image.new('RGB', (512, 512), bgr555(self.colors[0]))
        px = im.load()
        if layers == 'snes':
            for i in range(4096):
                tx, ty = (i % 64) * 8, (i // 64) * 8
                for y, row in enumerate(self.composite(i, unmask=False)):
                    for x, c in enumerate(row):
                        if c:
                            px[tx + x, ty + y] = bgr555(self.colors[c])
            return im
        for layer in layers:
            words = getattr(self, layer)
            for i, w in enumerate(words):
                tx, ty = (i % 64) * 8, (i // 64) * 8
                for y, row in enumerate(self.tile(w)):
                    for x, c in enumerate(row):
                        if c:
                            px[tx + x, ty + y] = bgr555(self.colors[c])
        return im
