"""
The co-op puzzle pieces: a Twin Stone and a Toss Bell.

New art, drawn here pixel by pixel (no ROM data), in the blend's cel-shaded
style: bold dark outline, flat color bands, a bright rim light.
- Twin Stone: a carved waystone with two small triangles cut into it. A sword
  strike lights the carving for a moment. A puzzle's stones must all be lit at
  once, and they stand too far apart for one player.
- Toss Bell: a golden bell under a little wooden frame. Only a partner thrown
  into it rings it.

Six frames: the stone dark, the stone lit (two glows), the bell still and
swinging both ways. Animations: 0 stone dark, 1 stone lit, 2 bell still,
3 bell ringing.

Writes (tmc/custom/gen, git-ignored like the other generators):
  coop/twin_relics.inc    sprite tiles, frames and animations (sprite SPRITE)
  coop/twin_relics.h      its palette, for the C code
  objlists/SPRITE.s       its frame object lists (spliced in by blend/anim/objlists.py)

    python3 blend/coop/twin_relics.py [--preview out.png]
"""
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
GEN = os.path.join(ROOT, 'tmc', 'custom', 'gen')

# After the Revive Fairy (338) in tmc/data/gfx/sprite_ptrs.s; keep both in step (blendCoop.h).
SPRITE = 339

PALETTE = [
    (0, 0, 0),        # . transparent
    (36, 24, 28),     # o outline
    (64, 70, 92),     # s stone, shadow
    (108, 118, 142),  # S stone
    (164, 176, 198),  # l stone, light
    (228, 236, 246),  # h rim light
    (34, 84, 110),    # c carving, dark
    (64, 196, 232),   # C carving, lit
    (214, 255, 255),  # w carving, glowing
    (132, 82, 22),    # D gold, dark
    (216, 152, 40),   # M gold
    (255, 226, 112),  # L gold, light
    (96, 56, 32),     # b wood, dark
    (156, 102, 56),   # B wood
    (255, 250, 190),  # y sparkle
    (224, 40, 48),    # r red (unused)
]
KEYS = '.osSlhcCwDMLbByr'

STONE = [
    '......oooo......',
    '.....oSllho.....',
    '....oSSllhho....',
    '....oSSSlllo....',
    '....oScSScSo....',
    '....occcccco....',
    '....osSccSlo....',
    '....osScSSlo....',
    '....osSSSSlo....',
    '....osSSSSlo....',
    '....ossSSSSo....',
    '...osssSSSSSo...',
    '..osssssSSSSSo..',
    '..oooooooooooo..',
    '................',
    '................',
]

BELL = [
    '.oooooooooooooo.',
    '.oBBBBBBBBBBBBo.',
    '.obbbbboobbbbbo.',
    '.oBo...oo...oBo.',
    '.oBo..oLLo..oBo.',
    '.oBo.oLMMDo.oBo.',
    '.oBo.oLMMDo.oBo.',
    '.oBooLMMMMDooBo.',
    '.oBooLMMMMDooBo.',
    '.oBooDDDDDDooBo.',
    '.oBo...oo...oBo.',
    '.oBo........oBo.',
    '.oBo........oBo.',
    'ooBoo......ooBoo',
    'ooooo......ooooo',
    '................',
]


def lit(rows, glow):
    """The stone with its carving lit: glow 0 is the first pulse, 1 the brighter one."""
    out = []
    for y, r in enumerate(rows):
        # the carving glows and the stone takes on its light
        if glow == 0:
            r = r.replace('c', 'C').replace('h', 'w')
            if y == 5:
                r = '....oCwwwwCo....'
        else:
            r = r.replace('c', 'w').replace('h', 'w').replace('l', 'C')
            if y == 5:
                r = '....owwwwwwo....'
        out.append(r)
    if glow == 1:
        # sparkles at the top corners
        out[1] = '..y' + out[1][3:13] + 'y..'
        out[0] = out[0][:2] + '.' + out[0][3:]
    else:
        out[3] = '..y' + out[3][3:13] + 'y..'
    return out


def swing(rows, dx):
    """The bell swung one pixel to the side, inside its frame, with a ring sparkle."""
    out = list(rows)
    for y in range(4, 11):
        inner = rows[y][4:12]
        inner = ('.' + inner[:-1]) if dx > 0 else (inner[1:] + '.')
        out[y] = rows[y][:4] + inner + rows[y][12:]
    sx = 2 if dx < 0 else 13
    out[11] = out[11][:sx] + 'y' + out[11][sx + 1:]
    return out


def frames():
    pics = [STONE, lit(STONE, 0), lit(STONE, 1), BELL, swing(BELL, -1), swing(BELL, 1)]
    out = []
    for p in pics:
        assert len(p) == 16 and all(len(r) == 16 for r in p), p
        out.append([[KEYS.index(c) for c in r] for r in p])
    return out


# (frame, duration) keys of each animation; an animation with one key holds it
ANIMS = [
    [(0, 255)],
    [(1, 8), (2, 8)],
    [(3, 255)],
    [(4, 6), (3, 4), (5, 6), (3, 4)],
]


def tiles16(pix):
    out = []
    for ty in range(2):
        for tx in range(2):
            b = bytearray(32)
            for y in range(8):
                for x in range(8):
                    b[y * 4 + x // 2] |= pix[ty * 8 + y][tx * 8 + x] << (4 * (x & 1))
            out.append(bytes(b))
    return out


def gba(c):
    r, g, b = c
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


def write_game(pics):
    os.makedirs(os.path.join(GEN, 'coop'), exist_ok=True)
    os.makedirs(os.path.join(GEN, 'objlists'), exist_ok=True)
    with open(os.path.join(GEN, 'coop', 'twin_relics.4bpp'), 'wb') as f:
        f.write(b''.join(b''.join(tiles16(p)) for p in pics))
    with open(os.path.join(GEN, 'coop', 'twin_relics.inc'), 'w') as f:
        f.write('@ Made by blend/coop/twin_relics.py: the Twin Stone and the Toss Bell.\n')
        f.write('\t.align 2\ngSprite_TwinRelics::\n\t.incbin "custom/gen/coop/twin_relics.4bpp"\n\n')
        f.write('\t.align 2\ngSpriteFrames_TwinRelics::\n')
        for i in range(len(pics)):
            f.write('\t.byte 4, 0\n\t.2byte %d\n' % (i * 4))
        for n, keys in enumerate(ANIMS):
            f.write('\ngSpriteAnimations_TwinRelics_%d:\n' % n)
            for i, (frame, dur) in enumerate(keys):
                f.write('\t.byte %d, %d, 0, 0x%x\n' % (frame, dur, 0x80 if i == len(keys) - 1 else 0))
            f.write('\t.byte %d, 0, 0, 0\n' % len(keys))
        f.write('\n\t.align 2\ngSpriteAnimations_TwinRelics::\n')
        for n in range(len(ANIMS)):
            f.write('\t.4byte gSpriteAnimations_TwinRelics_%d\n' % n)
        f.write('\t.4byte 0\n')
    with open(os.path.join(GEN, 'coop', 'twin_relics.h'), 'w') as f:
        f.write('/* Made by blend/coop/twin_relics.py */\n')
        f.write('static const u16 sTwinRelicsPalette[16] = {\n    ')
        f.write(', '.join('0x%04x' % gba(c) for c in PALETTE))
        f.write(',\n};\n')
    with open(os.path.join(GEN, 'objlists', '%d.s' % SPRITE), 'w') as f:
        f.write('@ Made by blend/coop/twin_relics.py\n\t.align 2\nBlendObjLists_%d:\n' % SPRITE)
        for i in range(len(pics)):
            f.write('\t.4byte BlendObjLists_%d_%d - gFrameObjLists\n' % (SPRITE, i))
        for i in range(len(pics)):
            f.write('BlendObjLists_%d_%d:\n\t.byte 1\n\t.byte -8, -14, 0x10\n\t.2byte 0\n' % (SPRITE, i))


def preview(pics, path):
    from PIL import Image, ImageDraw
    z = 12
    im = Image.new('RGB', (len(pics) * (16 * z + 24) + 24, 16 * z + 48), (58, 96, 64))
    d = ImageDraw.Draw(im)
    for n, p in enumerate(pics):
        ox = 24 + n * (16 * z + 24)
        for y in range(16):
            for x in range(16):
                if p[y][x]:
                    d.rectangle([ox + x * z, 24 + y * z, ox + x * z + z - 1, 24 + y * z + z - 1], fill=PALETTE[p[y][x]])
    im.save(path)


def main():
    pics = frames()
    write_game(pics)
    if '--preview' in sys.argv:
        preview(pics, sys.argv[sys.argv.index('--preview') + 1])
    print('Twin relics: %d frames (sprite %d)' % (len(pics), SPRITE))


if __name__ == '__main__':
    main()
