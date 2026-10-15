"""
Reads A Link to the Past's overworld as dumped by dumpow (blend/overworld/dumpow.c,
run by blend/dungeons/make-dungeons.sh over the user's own ROM): per area its 16x16
map (map16 numbers), background characters and colors, plus the tables that turn a
map16 into four 8x8 characters and a character into its tile attribute.
"""
import os
import struct

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DUMP = os.environ.get('ALTTP_OW', os.path.normpath(os.path.join(HERE, '..', 'out', 'z3', 'ow')))

# tile attributes Link can walk on (ground, grass, diggable ground, shallow water, stairs)
WALKABLE = {0x00, 0x09, 0x0d, 0x1d, 0x22, 0x3d, 0x3e, 0x3f, 0x40, 0x48, 0x4b}
DEEP_WATER = {0x08}
PIT = {0x20}


def available():
    return os.path.exists(os.path.join(DUMP, 'tables.bin'))


_tables = None


def tables():
    global _tables
    if _tables is None:
        d = open(os.path.join(DUMP, 'tables.bin'), 'rb').read()
        n = struct.unpack('<I', d[:4])[0]
        m16 = np.frombuffer(d[4:4 + n], '<u2').reshape(-1, 4).astype(np.int64)
        o = 4 + n
        n2 = struct.unpack('<I', d[o:o + 4])[0]
        attr = np.frombuffer(d[o + 4:o + 4 + n2], np.uint8)
        _tables = (m16, attr)
    return _tables


def snes_4bpp(data):
    a = np.frombuffer(data, np.uint8).reshape(-1, 32)
    out = np.zeros((a.shape[0], 8, 8), np.uint8)
    for y in range(8):
        p0, p1 = a[:, y * 2], a[:, y * 2 + 1]
        p2, p3 = a[:, 16 + y * 2], a[:, 16 + y * 2 + 1]
        for x in range(8):
            b = 7 - x
            out[:, y, x] = ((p0 >> b) & 1) | (((p1 >> b) & 1) << 1) | (((p2 >> b) & 1) << 2) | (((p3 >> b) & 1) << 3)
    return out


class Area:
    """One overworld area of A Link to the Past (0x00-0x3f Light World, 0x40-0x7f Dark World)."""

    def __init__(self, screen):
        d = open(os.path.join(DUMP, 'ow_%02x.bin' % screen), 'rb').read()
        self.screen = screen
        self.big = d[0]
        self.map = np.frombuffer(d[1:1 + 8192], '<u2').reshape(64, 64).astype(np.int64)
        vram = d[1 + 8192:1 + 8192 + 0x10000]
        self.chars = snes_4bpp(vram[0x4000:0xC000])
        pal = np.frombuffer(d[1 + 8192 + 0x10000:1 + 8192 + 0x10000 + 512], '<u2').astype(np.int64)
        self.pal = np.stack([(pal & 31) << 3, ((pal >> 5) & 31) << 3, ((pal >> 10) & 31) << 3], -1)
        self.size = 64 if self.big else 32

    def attrs(self, mx, my):
        """The four tile attributes (TL, TR, BL, BR) of the map16 at (mx, my)."""
        m16, attr = tables()
        return [int(attr[int(e) & 0x1ff]) for e in m16[self.map[my, mx]]]

    def render(self, x0, y0, w, h):
        """RGB pixels of map16 cells (x0, y0)..(x0 + w, y0 + h)."""
        m16, _ = tables()
        img = np.zeros((h * 16, w * 16, 3), np.int64)
        for my in range(h):
            for mx in range(w):
                m = self.map[y0 + my, x0 + mx]
                for k in range(4):
                    e = int(m16[m][k])
                    c, p = e & 0x3ff, (e >> 10) & 7
                    t = self.chars[c] if c < len(self.chars) else np.zeros((8, 8), np.uint8)
                    if e & 0x4000:
                        t = t[:, ::-1]
                    if e & 0x8000:
                        t = t[::-1, :]
                    py, px = my * 16 + (k >> 1) * 8, mx * 16 + (k & 1) * 8
                    blk = self.pal[p * 16 + t]
                    blk[t == 0] = self.pal[0]
                    img[py:py + 8, px:px + 8] = blk
        return img

    def common_map16(self, n):
        """The n most common map16 numbers of the area: its plain ground."""
        vals, counts = np.unique(self.map[:self.size, :self.size], return_counts=True)
        return set(int(v) for v in vals[np.argsort(-counts)][:n])
