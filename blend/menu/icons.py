"""
Menu icons for the A Link to the Past items.

Minish Cap draws every item's picture (pause menu, A/B buttons, items on the
ground or held up) from sprite 322. The A Link to the Past items had no
pictures there and borrowed Minish Cap ones (the Hammer showed the Smith's
Sword). This cuts each item's icon from A Link to the Past's own item menu
graphics, recolors it into the nearest of Minish Cap's item palettes, and adds
it to sprite 322 as a new frame. The Moon Pearl's menu tiles are not in the
same graphics set, so it gets new pixel art drawn here.

    python3 blend/menu/icons.py path/to/alttp-usa.sfc [--preview out.png]

Needs the tmc assets (make CUSTOM=1 build_assets). Writes, all git-ignored:
  tmc/custom/gen/ow/assets/sprites/gSprite_082E68F4.4bpp   the item graphics, new icons appended
  tmc/custom/gen/ow/assets/sprites/gSpriteFrames_322.s     the frame table, new frames appended
  tmc/custom/gen/menu/icon_anims.inc                       one animation per new icon
  tmc/custom/gen/objlists/322.s                            the frames' object lists
(custom/gen/ow/assets is searched before the build's own assets in CUSTOM builds.)
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
TMC = os.path.join(ROOT, 'tmc')
ASSETS = os.path.join(TMC, 'build', 'USA', 'assets')
GEN = os.path.join(TMC, 'custom', 'gen')
OVERRIDE = os.path.join(GEN, 'ow', 'assets', 'sprites')

SPRITE = 322

# Item id -> (label, A Link to the Past item-menu tilemap words: top-left, top-right,
# bottom-left, bottom-right; None = drawn below). Keep the ids in step with item.h and
# blend.h; the animation labels are used by tmc/data/animations/spriteAnimations322.s.
ICONS = [
    (0x05, 'IceRod', (0x2cb0, 0x2cbe, 0x2cc0, 0x2cc1)),
    (0x16, 'FireRod', (0x24b0, 0x24b1, 0x24c0, 0x24c1)),
    (0x18, 'Quake', (0x2866, 0x2867, 0xe867, 0xe866)),
    (0x19, 'Ether', (0x2876, 0x2877, 0xe877, 0xe876)),
    (0x1a, 'Bombos', (0x287d, 0x287e, 0xe87e, 0xe87d)),
    (0x32, 'MagicMirror', (0x2c62, 0x2c63, 0x2c72, 0x2c73)),
    (0x33, 'MoonPearl', None),
    (0x76, 'Hookshot', (0x24f5, 0x24f6, 0x24c0, 0x24f5)),
    (0x77, 'Hammer', (0x20b6, 0x20b7, 0x20c6, 0x20c7)),
    (0x78, 'CaneOfByrna', (0x2cdc, 0x2cdd, 0x2cec, 0x2ced)),
    (0x79, 'MagicPowder', (0x203b, 0x203c, 0x203d, 0x203e)),
    (0x7a, 'CaneOfSomaria', (0x24dc, 0x24dd, 0x24ec, 0x24ed)),
    (0x7c, 'BookOfMudora', (0x3ca5, 0x3ca6, 0x3cd8, 0x3cd9)),
    (0x7d, 'MagicCape', (0x24b4, 0x24b5, 0x24c4, 0x24c5)),
    (0x7e, 'BugNet', (0x3c40, 0x3c41, 0x2842, 0x3c43)),
    (0x7f, 'Mushroom', (0x2444, 0x2445, 0x2446, 0x2447)),
    (0x80, 'Shovel', (0x20d0, 0x20d1, 0x20e0, 0x20e1)),
    (0x81, 'TitansMitt', (0x28da, 0x28db, 0x28ea, 0x28eb)),
]

# The Moon Pearl, drawn for the blend: a pale pink pearl with a moon-shaped shine,
# in a gold setting. o outline, d/p/l/w pearl shades dark to white, y/Y gold.
PEARL = [
    '................',
    '......oooo......',
    '....oollllpo....',
    '...olwwllllpo...',
    '..olwwwlllllpo..',
    '..olwwllllllpo..',
    '.olllllllllllpo.',
    '.ollllllllllppo.',
    '.olllllllllpppo.',
    '.opllllllllpppo.',
    '..oplllllpppdo..',
    '..oopppppppddo..',
    '...oYYddddyyo...',
    '....oYYyyyyo....',
    '......oooo......',
    '................',
]
PEARL_COLORS = {'o': (40, 24, 32), 'd': (176, 128, 160), 'p': (224, 176, 208), 'l': (248, 224, 240),
                'w': (255, 255, 255), 'y': (200, 144, 40), 'Y': (248, 216, 96)}


def load_alttp(rom):
    sys.path.insert(0, os.path.join(ROOT, 'zelda3', 'assets'))
    import util
    import sprite_sheets as ss
    util.load_rom(rom)
    data = b''.join(bytes(ss.decode_2bit_tileset(s, height=64)) for s in (106, 107, 105))
    return data, util.get_words(0x9BD660, 64)


def snes_rgb(c):
    return ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)


def alttp_icon(gfx, hud_pal, words):
    """A 16x16 icon as rows of RGB tuples (None = transparent)."""
    img = [[None] * 16 for _ in range(16)]
    for k, w in enumerate(words):
        t, p = w & 0x3ff, (w >> 10) & 7
        s, tt = divmod(t, 128)
        for y in range(8):
            for x in range(8):
                sx = 7 - x if w & 0x4000 else x
                sy = 7 - y if w & 0x8000 else y
                v = gfx[s * 128 * 64 + ((tt // 16) * 8 + sy) * 128 + (tt % 16) * 8 + sx]
                if v:
                    img[(k // 2) * 8 + y][(k % 2) * 8 + x] = snes_rgb(hud_pal[p * 4 + v])
    return img


OUTLINE = (40, 24, 32)


def cut_out(img):
    """A Link to the Past's menu icons sit on the box's black: clear the black that
    reaches the edge, then outline what is left in Minish Cap's bold dark brown."""
    dark = lambda c: c is not None and max(c) < 24
    seen, todo = set(), [(x, y) for x in range(16) for y in (0, 15)] + [(x, y) for y in range(16) for x in (0, 15)]
    while todo:
        x, y = todo.pop()
        if (x, y) in seen or not (0 <= x < 16 and 0 <= y < 16):
            continue
        seen.add((x, y))
        if img[y][x] is None or dark(img[y][x]):
            img[y][x] = None
            todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    out = [row[:] for row in img]
    for y in range(16):
        for x in range(16):
            if img[y][x] is None and any(0 <= x + dx < 16 and 0 <= y + dy < 16 and img[y + dy][x + dx] is not None
                                         for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                out[y][x] = OUTLINE
    return out


def pearl_icon():
    return [[PEARL_COLORS[c] if c != '.' else None for c in row] for row in PEARL]


def item_palettes():
    """Minish Cap's item palettes, by the object list's palette offset (0-4)."""
    names = {k: 'gPalette_%d' % k for k in range(5)}
    pals = {}
    for off, name in names.items():
        raw = open(os.path.join(ASSETS, 'palettes', name + '.gbapal'), 'rb').read()
        pals[off] = [snes_rgb(struct.unpack_from('<H', raw, i * 2)[0]) for i in range(16)]
    return pals


def dist(a, b):
    # weighted so that hue shifts cost more than brightness ones
    return 2 * (a[0] - b[0]) ** 2 + 4 * (a[1] - b[1]) ** 2 + 3 * (a[2] - b[2]) ** 2


def quantize(img, pals):
    """(palette offset, 16x16 indices) using the item palette that fits the icon best."""
    best = None
    for off, pal in pals.items():
        usable = [i for i in range(1, 16) if pal[i] != (248, 0, 248)]
        err, out = 0, []
        for row in img:
            r = []
            for c in row:
                if c is None:
                    r.append(0)
                    continue
                i = min(usable, key=lambda k: dist(c, pal[k]))
                err += dist(c, pal[i])
                r.append(i)
            out.append(r)
        if best is None or err < best[0]:
            best = (err, off, out)
    return best[1], best[2]


def tiles16(pix):
    out = b''
    for ty in range(2):
        for tx in range(2):
            b = bytearray(32)
            for y in range(8):
                for x in range(8):
                    b[y * 4 + x // 2] |= pix[ty * 8 + y][tx * 8 + x] << (4 * (x & 1))
            out += bytes(b)
    return out


def frame0_list_offset():
    """Where frame 0's object list (one 16x16 object) sits in gFrameObjLists, and the sprite's offsets."""
    d = open(os.path.join(ASSETS, 'gfx', 'gFrameObjLists.bin'), 'rb').read()
    first = struct.unpack_from('<I', d, SPRITE * 4)[0]
    return d, first


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if not args:
        raise SystemExit(__doc__)
    gfx, hud_pal = load_alttp(args[0])
    pals = item_palettes()

    frames_src = open(os.path.join(ASSETS, 'sprites', 'gSpriteFrames_%d.s' % SPRITE)).read().rstrip('\n')
    nframes = frames_src.count('sprite_frame')
    tiles = open(os.path.join(ASSETS, 'sprites', 'gSprite_082E68F4.4bpp'), 'rb').read()
    first_tile = len(tiles) // 32

    # frame 0 is one 16x16 object at the icons' usual spot, with palette offset 4 (0x4000);
    # each new frame gets its own copy with its own palette offset
    d, first = frame0_list_offset()
    off0 = struct.unpack_from('<I', d, first)[0]
    n = d[off0]
    assert n == 1, 'frame 0 of sprite 322 is not a single object'
    x, y, b3, attr = struct.unpack_from('<bbBH', d, off0 + 1)

    new_tiles = b''
    new_frames, lists, anims, previews = [], [], [], []
    for k, (item, label, words) in enumerate(ICONS):
        img = pearl_icon() if words is None else cut_out(alttp_icon(gfx, hud_pal, words))
        off, pix = quantize(img, pals)
        new_tiles += tiles16(pix)
        frame = nframes + k
        new_frames.append('\tsprite_frame first_tile_index=0x%x, num_tiles=4' % (first_tile + 4 * k))
        lists.append((frame, off))
        anims.append((label, frame))
        previews.append((pix, pals[off]))

    os.makedirs(OVERRIDE, exist_ok=True)
    with open(os.path.join(OVERRIDE, 'gSprite_082E68F4.4bpp'), 'wb') as f:
        f.write(tiles + new_tiles)
    with open(os.path.join(OVERRIDE, 'gSpriteFrames_%d.s' % SPRITE), 'w') as f:
        f.write(frames_src + '\n@ blend/menu/icons.py: the A Link to the Past item icons\n' + '\n'.join(new_frames) + '\n')

    os.makedirs(os.path.join(GEN, 'menu'), exist_ok=True)
    with open(os.path.join(GEN, 'menu', 'icon_anims.inc'), 'w') as f:
        f.write('@ Made by blend/menu/icons.py: one still animation per A Link to the Past item icon.\n')
        for label, frame in anims:
            f.write('gSpriteAnimations_322_Blend%s::\n\tkeyframe frame_index=%d, duration=255, bitfield2=0x80\n'
                    '\t.byte 1 @ keyframe count\n' % (label, frame))

    # sprite 322's object lists: the game's own, then the new frames' lists
    os.makedirs(os.path.join(GEN, 'objlists'), exist_ok=True)
    with open(os.path.join(GEN, 'objlists', '%d.s' % SPRITE), 'w') as f:
        f.write('@ Made by blend/menu/icons.py\n\t.align 2\nBlendObjLists_%d:\n' % SPRITE)
        for i in range(nframes):
            f.write('\t.4byte 0x%x\n' % struct.unpack_from('<I', d, first + i * 4)[0])
        for frame, off in lists:
            f.write('\t.4byte BlendObjLists_%d_%d - gFrameObjLists\n' % (SPRITE, frame))
        for frame, off in lists:
            f.write('BlendObjLists_%d_%d:\n\t.byte 1\n\t.byte %d, %d, 0x%x\n\t.2byte 0x%x\n'
                    % (SPRITE, frame, x, y, b3, (attr & 0x0fff) | (off << 12)))

    if '--preview' in sys.argv:
        from PIL import Image
        z = 4
        im = Image.new('RGB', (len(previews) * 20 * z, 20 * z), (72, 64, 80))
        for n, (pix, pal) in enumerate(previews):
            for yy in range(16):
                for xx in range(16):
                    if pix[yy][xx]:
                        for q in range(z * z):
                            im.putpixel(((n * 20 + 2 + xx) * z + q % z, (2 + yy) * z + q // z), pal[pix[yy][xx]])
        im.save(sys.argv[sys.argv.index('--preview') + 1])
    print('Menu icons: %d A Link to the Past items (sprite %d frames %d-%d)'
          % (len(ICONS), SPRITE, nframes, nframes + len(ICONS) - 1))


if __name__ == '__main__':
    main()
