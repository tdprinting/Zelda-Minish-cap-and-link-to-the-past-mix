"""
Builds Link's animations for the A Link to the Past items.

Each new frame is a Minish Cap Link pose with the item, taken from A Link to
the Past's own item graphics and recolored to Link's Minish Cap palette, drawn
into his hands. The poses follow A Link to the Past: the rods and the Cane of
Somaria are raised and swung forward, the Hammer goes up over Link's head and
slams down, the Hookshot is held out in front, the Cane of Byrna is held up,
Magic Powder is shaken from its bag, and the medallions and the Magic Mirror
are held up high.

    python3 blend/anim/build.py path/to/alttp-usa.sfc [--preview out.png]

Needs the tmc assets (make CUSTOM=1 build_assets). Writes tmc/custom/gen/:
  blend_link.inc       Link sprite 159: tiles, frames and animations
  objlists/159.s       its frame object lists (spliced in by objlists.py)
Everything written there comes from your ROMs and stays out of git.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
sys.path.insert(0, HERE)
import mcsprite as S  # noqa: E402

SPRITE = 159          # the null gSpritePtrs slot these frames live in
GEN = os.path.join(ROOT, 'tmc', 'custom', 'gen')
MAX_TILES = 22        # Link's frames never use more tiles than this

# ---------------------------------------------------------------- ALttP art


def load_alttp(rom):
    sys.path.insert(0, os.path.join(ROOT, 'zelda3', 'assets'))
    import util
    import sprite_sheets as ss
    util.load_rom(rom)
    return ss


def strip(ss, sheets, t, n):
    """An item's graphics as A Link to the Past loads them: n 8x16 columns from tile t."""
    data = b''.join(bytes(ss.decode_3bit_tileset(s)) for s in sheets)

    def px(tt, x, y):
        s, tt = divmod(tt, 64)
        return data[s * 4096 + ((tt // 16) * 8 + y) * 128 + (tt % 16) * 8 + x] % 8
    return [[px(t + k + 16 * (y // 8), x % 8, y % 8) for k in range(n) for x in range(8)] for y in range(16)]


def crop(img, x0, y0, x1, y1):
    return [row[x0:x1] for row in img[y0:y1]]


def recolor(img, cmap):
    return [[cmap.get(v, 0) if v else 0 for v in row] for row in img]


def trim(img):
    """Crops away empty rows and columns; returns (img, dx, dy) of the crop."""
    ys = [y for y, row in enumerate(img) if any(row)]
    xs = [x for x in range(len(img[0])) if any(row[x] for row in img)]
    return crop(img, xs[0], ys[0], xs[-1] + 1, ys[-1] + 1), xs[0], ys[0]


def shrink(img, w, h):
    """A smaller copy, sampled from the middle of each source pixel block."""
    sh, sw = len(img), len(img[0])
    return [[img[min(sh - 1, (2 * y + 1) * sh // (2 * h))][min(sw - 1, (2 * x + 1) * sw // (2 * w))]
             for x in range(w)] for y in range(h)]


def flip_h(img):
    return [row[::-1] for row in img]


def flip_v(img):
    return img[::-1]


def rot_ccw(img):
    """Rotates a quarter turn so that what pointed up points left."""
    h, w = len(img), len(img[0])
    return [[img[y][w - 1 - x] for y in range(h)] for x in range(w)]


# Minish Cap Link palette (OBJ palette 6): 1 red, 2 dark red, 3/4/5 greens,
# 6 blue, 7 pale green, 8 gold, 9 yellow, 10/11 browns, 12/13 skin, 14 white, 15 black.
OUT = 15
COLORS = {
    'fire_rod': {1: 12, 3: 2, 4: 1, 5: OUT},
    'ice_rod': {1: 12, 3: 6, 4: 14, 5: OUT},
    'hammer': {1: 14, 2: 6, 6: 11, 5: OUT},
    'hookshot': {1: 14, 3: 2, 4: 1, 6: 6, 7: 14, 5: OUT},
    'somaria': {1: 14, 3: 1, 4: 13, 5: OUT},
    'byrna': {1: 14, 3: 6, 4: 14, 5: OUT},
    'powder': {1: 14, 2: 1, 6: 10, 7: 11, 5: OUT},
    'bombos': {5: 10, 6: 8, 7: 9},
    'ether': {5: OUT, 6: 6, 7: 14},
    'quake': {5: OUT, 6: 3, 7: 5},
    'mirror': {1: 14, 3: 6, 4: 8, 6: 7, 7: 6, 5: OUT},
}


class Item:
    """An item drawn pointing up (grip at the bottom) and diagonally up-forward."""

    def __init__(self, up, diag=None, grip_up=None, grip_diag=None):
        self.up, self.diag = up, diag
        self.grip_up = grip_up or (len(up[0]) // 2, len(up) - 1)
        if diag is not None:
            self.grip_diag = grip_diag or (0, len(diag) - 1)

    def oriented(self, how):
        """(image, grip) for one direction: up, down, left, right, ul, ur, dl, dr."""
        img, (gx, gy) = self.up, self.grip_up
        h, w = len(img), len(img[0])
        if how == 'up':
            return img, (gx, gy)
        if how == 'down':
            return flip_v(img), (gx, h - 1 - gy)
        if how == 'left':
            return rot_ccw(img), (gy, w - 1 - gx)
        if how == 'right':
            r = rot_ccw(img)
            return flip_h(r), (h - 1 - gy, w - 1 - gx)
        img, (gx, gy) = self.diag, self.grip_diag
        h, w = len(img), len(img[0])
        if how == 'ur':
            return img, (gx, gy)
        if how == 'ul':
            return flip_h(img), (w - 1 - gx, gy)
        if how == 'dr':
            return flip_v(img), (gx, h - 1 - gy)
        if how == 'dl':
            return flip_v(flip_h(img)), (w - 1 - gx, h - 1 - gy)
        raise ValueError(how)


def make_items(ss):
    rod = strip(ss, [0], 0, 7)
    hammer = strip(ss, [0], 11, 7)
    hook = strip(ss, [0], 34, 4)
    cane = strip(ss, [96], 33, 7)

    def icon(t):
        return strip(ss, [0x5a, 0x5b], t, 2)

    items = {}
    for name in ('fire_rod', 'ice_rod'):
        c = COLORS[name]
        items[name] = Item(trim(recolor(crop(rod, 16, 0, 24, 16), c))[0], trim(recolor(crop(rod, 0, 0, 16, 16), c))[0])
    c = COLORS['hammer']
    items['hammer'] = Item(trim(recolor(crop(hammer, 16, 0, 24, 16), c))[0], trim(recolor(crop(hammer, 0, 0, 16, 16), c))[0])
    for name in ('somaria', 'byrna'):
        c = COLORS[name]
        items[name] = Item(trim(recolor(crop(cane, 16, 0, 24, 16), c))[0], trim(recolor(crop(cane, 0, 0, 16, 16), c))[0])
    # the Hookshot: its claw on top of its barrel, pointing up
    c = COLORS['hookshot']
    claw = recolor(crop(hook, 0, 0, 16, 8), c)
    barrel = recolor(crop(hook, 8, 9, 16, 16), c)   # a 4-pixel barrel at x 2..5
    up = [row[:] for row in claw] + [[0] * 16 for _ in range(7)]
    for y in range(7):
        for x in range(8):
            if barrel[y][x]:
                up[8 + y][x + 4] = barrel[y][x]
    items['hookshot'] = Item(trim(up)[0])
    # the round pictures A Link to the Past shows when Link receives them
    for name, t in (('bombos', 98), ('ether', 96), ('quake', 100), ('mirror', 66), ('powder', 38)):
        img = trim(recolor(icon(t), COLORS[name]))[0]
        if name == 'powder':
            # a hand-sized bag, as small as the one Link shakes in A Link to the Past
            img = shrink(img, 10, 10)
            items[name] = Item(img, grip_up=(len(img[0]) // 2, 1))   # held by the string at the top
        else:
            items[name] = Item(img)
    return items


# ---------------------------------------------------------------- MC frames


def mc_frame(sprite, frame):
    """Link's frame as {(x, y): color index} around his origin, plus its frame record."""
    gfx = mc_frame.gfx
    nt, unk, first = S.sprite_frames(sprite)[frame]
    pix = {}
    for (x, y, w, h, t, hf, vf) in S.frame_objs(sprite, frame):
        tw = w // 8
        for ty in range(h // 8):
            for tx in range(tw):
                p = S.tile(gfx, first + t + ty * tw + tx)
                for py in range(8):
                    for px in range(8):
                        c = p[py][px]
                        if not c:
                            continue
                        sx, sy = tx * 8 + px, ty * 8 + py
                        if hf:
                            sx = w - 1 - sx
                        if vf:
                            sy = h - 1 - sy
                        pix[(x + sx, y + sy)] = c
    return pix, unk


def obj_b3(sprite, frame):
    d = S.obj_lists()
    first = struct.unpack_from('<I', d, sprite * 4)[0]
    off = struct.unpack_from('<I', d, first + frame * 4)[0]
    return d[off + 1 + 2] & 0x03   # keep the low (palette) bits of the first object


# ---------------------------------------------------------------- poses
# A pose is a Minish Cap frame (sprite, frame) and the items drawn on it:
# (item orientation, hand x, hand y, 'front' or 'back'). Hand positions are in
# pixels from Link's origin (his feet); side poses face left as Minish Cap's do.

D_UP2 = (1, 86)       # both arms up, facing down
D_UP2B = (1, 87)
D_ARMS = (1, 85)
D_RAISE = (2, 229)    # one arm raised, facing down
D_OUT = (2, 5)        # arm out to the side
D_BOW = (6, 149)      # bowed forward after a downward stroke
D_HOLD = (5, 2)       # both hands in front (Gust Jar)
S_BACK = (6, 156)     # side, hand drawn back by the head
S_MID = (6, 155)
S_FWD = (6, 158)      # side, arm straight out in front
S_FWD2 = (2, 10)
U_RAISE = (6, 165)    # facing up, one hand raised
U_SIDE = (2, 13)
U_HOLD = (5, 60)      # facing up, hands up in front

POSES = {
    # rods and the Cane of Somaria: raised up, swung forward
    'rod': {
        'down': [(D_RAISE, 'up', 7, -17, 'front'), (D_OUT, 'dr', 8, -6, 'front'), (D_BOW, 'down', 2, -1, 'front')],
        'side': [(S_BACK, 'up', 1, -13, 'back'), (S_MID, 'ul', -10, -8, 'front'), (S_FWD, 'left', -15, -5, 'front')],
        'up': [(U_RAISE, 'up', -6, -17, 'front'), (U_SIDE, 'ul', -8, -9, 'back'), (U_HOLD, 'up', 0, -17, 'back')],
    },
    # the Hammer: lifted over the head with both hands, slammed down in front
    'hammer': {
        'down': [(D_UP2, 'up', 0, -19, 'back'), (D_OUT, 'dr', 8, -6, 'front'), (D_BOW, 'down', 2, -1, 'front')],
        'side': [(S_BACK, 'up', 1, -13, 'back'), (S_MID, 'ul', -10, -8, 'front'), (S_FWD, 'dl', -15, -5, 'front')],
        'up': [(U_RAISE, 'up', -6, -17, 'front'), (U_SIDE, 'ul', -8, -9, 'back'), (U_HOLD, 'up', 0, -15, 'back')],
    },
    # the Cane of Byrna: held up high
    'raise': {
        'down': [(D_RAISE, 'up', 7, -17, 'front'), (D_UP2, 'up', 0, -19, 'back')],
        'side': [(S_BACK, 'up', 1, -13, 'back'), (S_BACK, 'up', 1, -16, 'back')],
        'up': [(U_RAISE, 'up', -6, -17, 'front'), (U_RAISE, 'up', -6, -19, 'front')],
    },
    # the Hookshot: held straight out in front
    'hold': {
        'down': [(D_HOLD, 'down', 0, -3, 'front')],
        'side': [(S_FWD2, 'left', -9, -5, 'front')],
        'up': [(U_HOLD, 'up', 0, -12, 'back')],
    },
    # Magic Powder: the bag held out and shaken
    'shake': {
        'down': [(D_HOLD, 'up', 0, -6, 'front'), (D_HOLD, 'up', 0, -2, 'front')],
        'side': [(S_FWD2, 'up', -10, -7, 'front'), (S_FWD2, 'up', -10, -3, 'front')],
        'up': [(U_HOLD, 'up', 0, -25, 'front'), (U_HOLD, 'up', 0, -27, 'front')],
    },
    # medallions and the Mirror: held up over the head, facing out
    'hold_up': {
        'down': [(D_ARMS, None, 0, 0, 'front'), (D_UP2B, 'up', 0, -17, 'front'), (D_UP2, 'up', 0, -19, 'front')],
    },
}

# Keyframes per pose family: (pose index, duration, playerFrame flags).
# The item acts on the frame flagged 0x40 and ends on 0x80, as before.
TIMING = {
    'rod': [(0, 4, 0), (1, 2, 0), (2, 1, 0x40), (2, 10, 0), (2, 1, 0x80)],
    'hammer': [(0, 8, 0), (1, 2, 0), (2, 1, 0x40), (2, 14, 0), (2, 1, 0x80)],
    'raise': [(0, 3, 0), (1, 1, 0x40), (1, 14, 0), (0, 3, 0), (0, 1, 0x80)],
    'hold': [(0, 2, 0), (0, 1, 0x40), (0, 10, 0), (0, 1, 0x80)],
    'shake': [(0, 4, 0), (1, 4, 0), (0, 3, 0), (1, 1, 0x40), (1, 3, 0), (0, 4, 0), (1, 4, 0), (1, 1, 0x80)],
    'hold_up': [(0, 3, 0), (1, 4, 0), (2, 20, 0), (2, 1, 0x40), (2, 16, 0), (2, 1, 0x80)],
}

# Animation groups in order: the id is 0x9F00 + 4 * index (keep in sync with blend.h).
GROUPS = [
    ('hammer', 'hammer'), ('hookshot', 'hold'), ('fire_rod', 'rod'), ('ice_rod', 'rod'),
    ('somaria', 'rod'), ('byrna', 'raise'), ('powder', 'shake'),
    ('bombos', 'hold_up'), ('ether', 'hold_up'), ('quake', 'hold_up'), ('mirror', 'hold_up'),
]


def composite(pose, item, items):
    base, how, hx, hy, layer = pose
    body, unk = mc_frame(*base)
    pix = dict(body)
    if how is not None:
        img, (gx, gy) = items[item].oriented(how)
        ox, oy = hx - gx, hy - gy
        for y, row in enumerate(img):
            for x, c in enumerate(row):
                if c and (layer == 'front' or (ox + x, oy + y) not in body):
                    pix[(ox + x, oy + y)] = c
    return pix, unk, obj_b3(*base)


# ---------------------------------------------------------------- packing

SHAPES = {(16, 16): (0, 1), (16, 8): (1, 0), (8, 16): (2, 0), (8, 8): (0, 0)}


def pack(pix):
    """Cuts a frame into objects. Returns ([(x, y, w, h)], [tile bytes...])."""
    xs = [x for x, _ in pix]
    ys = [y for _, y in pix]
    x0, y0 = min(xs), min(ys)
    cells = {((x - x0) // 8, (y - y0) // 8) for (x, y) in pix}
    used = set()
    objs = []
    cw = max(c[0] for c in cells) + 1
    ch = max(c[1] for c in cells) + 1
    for by in range(0, ch, 2):
        for bx in range(0, cw, 2):
            quad = [(bx, by), (bx + 1, by), (bx, by + 1), (bx + 1, by + 1)]
            have = [q in cells for q in quad]
            if sum(have) >= 3:
                objs.append((bx, by, 16, 16))
                used.update(quad)
                continue
            if have[0] and have[1]:
                objs.append((bx, by, 16, 8))
                used.update(quad[:2])
            if have[2] and have[3]:
                objs.append((bx, by + 1, 16, 8))
                used.update(quad[2:])
            for c in quad:
                if c in cells and c not in used:
                    below = (c[0], c[1] + 1)
                    if c[1] % 2 == 0 and below in cells and below not in used:
                        objs.append((c[0], c[1], 8, 16))
                        used.update([c, below])
                    else:
                        objs.append((c[0], c[1], 8, 8))
                        used.add(c)
    tiles = []
    out = []
    for (cx, cy, w, h) in objs:
        ox, oy = x0 + cx * 8, y0 + cy * 8
        out.append((ox, oy, w, h, len(tiles)))
        for ty in range(h // 8):
            for tx in range(w // 8):
                b = bytearray(32)
                for py in range(8):
                    for px in range(8):
                        c = pix.get((ox + tx * 8 + px, oy + ty * 8 + py), 0)
                        b[py * 4 + px // 2] |= c << (4 * (px & 1))
                tiles.append(bytes(b))
    return out, tiles


# ---------------------------------------------------------------- output


def build(rom, preview=None):
    ss = load_alttp(rom)
    items = make_items(ss)
    mc_frame.gfx = open(os.path.join(S.ASSETS, 'sprites', 'gSprite_Link.4bpp'), 'rb').read()

    frames = []        # (objs, first tile, num tiles, unk, b3)
    frame_ids = {}
    gfx = []
    pictures = []      # for the preview: (label, pix)
    anims = []         # per animation entry: [(frame, duration, bitfield, flags)]

    def frame_for(pose, item, label):
        key = (pose, item)
        if key not in frame_ids:
            pix, unk, b3 = composite(pose, item, items)
            objs, tiles = pack(pix)
            if len(tiles) > MAX_TILES:
                raise SystemExit('%s: %d tiles is more than Link can show' % (label, len(tiles)))
            frame_ids[key] = len(frames)
            frames.append((objs, len(gfx), len(tiles), unk, b3))
            gfx.extend(tiles)
            pictures.append((label, pix))
        return frame_ids[key]

    for item, family in GROUPS:
        poses = POSES[family]
        dirs = [('up', 'up'), ('side', 'right'), ('down', 'down'), ('side', 'left')]
        for k, (d, _) in enumerate(dirs):
            if k == 3:
                anims.append(None)  # left reuses right, flipped by the game
                continue
            ps = poses.get(d) or poses['down']
            side = d == 'side' and d in poses
            keys = []
            for (i, dur, flags) in TIMING[family]:
                f = frame_for(ps[i], item, '%s %s %d' % (item, d if d in poses else 'down', i))
                keys.append((f, dur, 0x40 if side else 0, flags))
            anims.append(keys)

    # tiles, frames and animations
    os.makedirs(os.path.join(GEN, 'objlists'), exist_ok=True)
    with open(os.path.join(GEN, 'blend_link.4bpp'), 'wb') as f:
        f.write(b''.join(gfx))
    with open(os.path.join(GEN, 'blend_link.inc'), 'w') as f:
        f.write('@ Made by blend/anim/build.py: Link with the A Link to the Past items.\n')
        f.write('\t.align 2\ngSprite_BlendLink::\n\t.incbin "custom/gen/blend_link.4bpp"\n\n')
        f.write('\t.align 2\ngSpriteFrames_BlendLink::\n')
        for (objs, first, n, unk, b3) in frames:
            f.write('\t.byte %d, %d\n\t.2byte %d\n' % (n, unk, first))
        f.write('\n')
        for i, keys in enumerate(anims):
            if keys is None:
                continue
            f.write('gSpriteAnimations_BlendLink_%d:\n' % i)
            for (fr, dur, bits, flags) in keys:
                f.write('\t.byte %d, %d, 0x%x, 0x%x\n' % (fr, dur, bits, flags))
            f.write('\t.byte 1, 0, 0, 0\n')  # loop back onto the last keyframe
        f.write('\n\t.align 2\ngSpriteAnimations_BlendLink::\n')
        for i, keys in enumerate(anims):
            f.write('\t.4byte gSpriteAnimations_BlendLink_%d\n' % (i - 2 if keys is None else i))
        f.write('\t.4byte 0\n')
    with open(os.path.join(GEN, 'objlists', '%d.s' % SPRITE), 'w') as f:
        f.write('@ Made by blend/anim/build.py\n\t.align 2\nBlendObjLists_%d:\n' % SPRITE)
        for i in range(len(frames)):
            f.write('\t.4byte BlendObjLists_%d_%d - gFrameObjLists\n' % (SPRITE, i))
        for i, (objs, first, n, unk, b3) in enumerate(frames):
            f.write('BlendObjLists_%d_%d:\n\t.byte %d\n' % (SPRITE, i, len(objs)))
            for (x, y, w, h, t) in objs:
                shape, size = SHAPES[(w, h)]
                f.write('\t.byte %d, %d, 0x%02x\n\t.2byte %d\n' % (x, y, b3 | (size << 4) | (shape << 6), t))
    print('Link item animations: %d frames, %d tiles, %d animations' % (len(frames), len(gfx), len(anims)))
    if preview:
        draw_preview(pictures, preview)


def draw_preview(pictures, path):
    from PIL import Image, ImageDraw
    # LINK_PAL: a hex dump of the OBJ palettes (the harness's peek of 0x020178A0); grey without one
    if os.environ.get('LINK_PAL'):
        raw = bytes.fromhex(open(os.environ['LINK_PAL']).read().split('\n')[0].replace(' ', ''))
        pal = [((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)
               for c in struct.unpack('<16H', raw[6 * 32:7 * 32])]
    else:
        pal = [(255 - i * 16,) * 3 for i in range(16)]
    Z, cw, chh = 4, 48, 56
    cols = 10
    sheet = Image.new('RGB', (cols * cw * Z, ((len(pictures) + cols - 1) // cols) * (chh * Z + 14)), (40, 40, 48))
    d = ImageDraw.Draw(sheet)
    for k, (label, pix) in enumerate(pictures):
        im = Image.new('RGB', (cw, chh), (100, 110, 130))
        for (x, y), c in pix.items():
            X, Y = x + cw // 2, y + 44
            if 0 <= X < cw and 0 <= Y < chh:
                im.putpixel((X, Y), pal[c])
        X, Y = (k % cols) * cw * Z, (k // cols) * (chh * Z + 14)
        sheet.paste(im.resize((cw * Z, chh * Z), 0), (X, Y))
        d.text((X + 3, Y + chh * Z + 1), label, fill=(255, 255, 0))
    sheet.save(path)


if __name__ == '__main__':
    args = sys.argv[1:]
    prev = None
    if '--preview' in args:
        i = args.index('--preview')
        prev = args[i + 1]
        del args[i:i + 2]
    if not args:
        raise SystemExit(__doc__)
    build(args[0], prev)
