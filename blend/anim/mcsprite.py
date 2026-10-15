"""
Reads and draws Minish Cap sprites from the tmc build: frame tables, the object
(OAM) lists each frame is built from, and the sprite graphics.
"""
import os
import struct

from PIL import Image

TMC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tmc')
ASSETS = os.path.join(TMC, 'build', 'USA', 'assets')

# object shape/size -> (width, height) in pixels
DIMS = {(0, 0): (8, 8), (0, 1): (16, 16), (0, 2): (32, 32), (0, 3): (64, 64),
        (1, 0): (16, 8), (1, 1): (32, 8), (1, 2): (32, 16), (1, 3): (64, 32),
        (2, 0): (8, 16), (2, 1): (8, 32), (2, 2): (16, 32), (2, 3): (32, 64)}

_obj = None


def obj_lists():
    global _obj
    if _obj is None:
        _obj = open(os.path.join(ASSETS, 'gfx', 'gFrameObjLists.bin'), 'rb').read()
    return _obj


def frame_objs(sprite, frame):
    """[(x, y, w, h, tile, hflip, vflip)] for one frame of a sprite."""
    d = obj_lists()
    first = struct.unpack_from('<I', d, sprite * 4)[0]
    off = struct.unpack_from('<I', d, first + frame * 4)[0]
    n = d[off]
    out = []
    for i in range(n):
        x, y, b3, b4 = struct.unpack_from('<bbBH', d, off + 1 + i * 5)
        w, h = DIMS[(b3 >> 6, (b3 >> 4) & 3)]
        out.append((x, y, w, h, b4 & 0x3ff, bool(b3 & 4), bool(b3 & 8)))
    return out


def sprite_frames(n):
    data = open(os.path.join(ASSETS, 'sprites', 'gSpriteFrames_%d.bin' % n), 'rb').read()
    return [struct.unpack_from('<BBH', data, i) for i in range(0, len(data) - 3, 4)]


def tile(gfx, index):
    b = gfx[index * 32:index * 32 + 32].ljust(32, b'\0')
    return [[(b[y * 4 + x // 2] >> (4 * (x & 1))) & 15 for x in range(8)] for y in range(8)]


def draw(gfx, first_tile, objs, pal, size=64, origin=(32, 40), image=None):
    im = image or Image.new('RGBA', (size, size), (0, 0, 0, 0))
    for (x, y, w, h, t, hf, vf) in objs:
        tw = w // 8
        for ty in range(h // 8):
            for tx in range(tw):
                pix = tile(gfx, first_tile + t + ty * tw + tx)
                for py in range(8):
                    for px in range(8):
                        c = pix[py][px]
                        if not c:
                            continue
                        sx = tx * 8 + px
                        sy = ty * 8 + py
                        if hf:
                            sx = w - 1 - sx
                        if vf:
                            sy = h - 1 - sy
                        X, Y = origin[0] + x + sx, origin[1] + y + sy
                        if 0 <= X < im.width and 0 <= Y < im.height:
                            im.putpixel((X, Y), pal[c] + (255,))
    return im
