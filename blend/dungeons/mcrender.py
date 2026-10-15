"""
Draws Minish Cap rooms offline from a tileset's graphics, metatiles and palettes,
so tile work can be checked without running the game.
"""
import os
import struct

from PIL import Image

import mctiles

ASSETS = os.path.join(mctiles.TMC, 'build', 'USA', 'assets')


def gfx(area_prefix, tileset='0'):
    d = os.path.join(mctiles.area_dir(area_prefix), 'tileSets')
    data = b''
    for part in range(3):
        name = [f for f in os.listdir(os.path.join(d, tileset)) if f.endswith('_%s_%d.4bpp' % (tileset, part))][0]
        data += open(os.path.join(d, tileset, name), 'rb').read().ljust(0x4000, b'\0')
    return data


def palettes(first, count=13, dest=2):
    pal = [[(0, 0, 0)] * 16 for _ in range(16)]
    for i in range(count):
        raw = open(os.path.join(ASSETS, 'palettes', 'gPalette_%d.gbapal' % (first + i)), 'rb').read()
        cols = struct.unpack('<16H', raw[:32])
        pal[dest + i] = [((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3) for c in cols]
    return pal


def metatiles(area_prefix, layer):
    d = os.path.join(mctiles.area_dir(area_prefix), 'tileSet')
    name = [f for f in os.listdir(d) if f.startswith('gAreaTileSet_') and f.endswith('_%s.bin' % layer)][0]
    return mctiles.u16s(os.path.join(d, name))


def tile_pixels(chars, index):
    b = chars[index * 32:index * 32 + 32]
    return [[(b[y * 4 + x // 2] >> (4 * (x & 1))) & 15 for x in range(8)] for y in range(8)]


class Renderer:
    def __init__(self, chars, pal, meta_bottom, meta_top, base_bottom=0, base_top=0):
        self.chars, self.pal = chars, pal
        self.meta = {'bottom': meta_bottom, 'top': meta_top}
        self.base = {'bottom': base_bottom, 'top': base_top}

    def draw_entry(self, im, px, py, entry, base, transparent):
        idx = (entry & 0x3ff) + base
        hf, vf, p = entry & 0x400, entry & 0x800, entry >> 12
        pix = tile_pixels(self.chars, idx) if idx * 32 < len(self.chars) else [[0] * 8] * 8
        for y in range(8):
            for x in range(8):
                c = pix[7 - y if vf else y][7 - x if hf else x]
                if c == 0 and transparent:
                    continue
                im.putpixel((px + x, py + y), self.pal[p][c] if c else self.pal[0][0])

    def room(self, bottom, top, w, h):
        im = Image.new('RGB', (w * 16, h * 16))
        for layer, cells, transparent in (('bottom', bottom, False), ('top', top, True)):
            meta = self.meta[layer]
            for i, t in enumerate(cells):
                if t >= len(meta) // 4 or (transparent and t == 0):
                    continue
                x, y = (i % w) * 16, (i // w) * 16
                for k in range(4):
                    self.draw_entry(im, x + (k & 1) * 8, y + (k >> 1) * 8, meta[t * 4 + k], self.base[layer], transparent)
        return im
