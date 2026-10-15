"""
The Revive Fairy: the co-op item that brings a fallen partner back.

New art, drawn here pixel by pixel (no ROM data): a corked glass jar in the
blend's cel-shaded style (bold dark outline, flat color bands, bright rim
light) holding a pink fairy with a gold glow, and a small red heart on the
lid. Three frames: the wings beat and the glow pulses.

Writes, for the game (tmc/custom/gen, git-ignored like the other generators):
  coop/revive_fairy.inc   sprite tiles, frames and animations (sprite SPRITE)
  coop/revive_fairy.h     its palette, for the C code
  objlists/SPRITE.s       its frame object lists (spliced in by blend/anim/objlists.py)
and, for the co-op player on the PC:
  blend/host/revive_art.h the same pictures, as RGBA

    python3 blend/coop/revive_fairy.py [--preview out.png]
"""
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
GEN = os.path.join(ROOT, 'tmc', 'custom', 'gen')
HOST = os.path.join(ROOT, 'blend', 'host', 'revive_art.h')

# The sprite slot the frames live in, appended after the A Link to the Past
# sprites in tmc/data/gfx/sprite_ptrs.s (keep both in step; see blendCoop.h).
SPRITE = 338

# 0 is transparent. The outline matches the dungeons' brown-black.
PALETTE = [
    (0, 0, 0),        # . transparent
    (36, 24, 28),     # o outline
    (26, 72, 96),     # d glass, shadow side
    (58, 140, 168),   # G glass
    (150, 220, 232),  # g glass, light
    (240, 252, 255),  # h rim light
    (132, 82, 22),    # D gold, dark
    (216, 152, 40),   # M gold
    (255, 226, 112),  # L gold, light
    (178, 46, 104),   # p fairy, dark
    (240, 108, 164),  # P fairy
    (255, 206, 228),  # k fairy, light
    (255, 255, 255),  # w wing
    (150, 190, 255),  # b wing, blue
    (224, 40, 48),    # r heart
    (255, 240, 136),  # y glow
]
KEYS = '.odGghDMLpPkwbry'

TOP = [
    '......oooo......',
    '.....oLLMMo.....',
    '....oLrrrMDo....',
    '....oDMrMDDo....',
    '.....ohggdo.....',
    '....ohGGGGdo....',
    '...ohGGGGGGdo...',
]
BOTTOM = [
    '..odGGGGGGGddo..',
    '...oddddddddo...',
    '....oooooooo....',
]
# rows 7 to 12, the inside of the jar (columns 4 to 11): the fairy, three wingbeats
FAIRY = [
    ['bwGkkGwb', 'bwykkywb', 'GbyPPybG', 'GGyPPyGG', 'GGGppGGG', 'GGGyyGGG'],
    ['GGykkyGG', 'bwykkywb', 'bwyPPywb', 'GGyPPyGG', 'GGGppGGG', 'GGGGGGGG'],
    ['GGGkkGGG', 'GGykkyGG', 'bwyPPywb', 'bwyPPywb', 'GGyppyGG', 'GGGyyGGG'],
]
# frame durations (1/60 s); the glow lingers on the middle beat
DURATIONS = [10, 6, 10]


def frames():
    out = []
    for f in FAIRY:
        rows = TOP + ['..oh%sdo..' % r for r in f] + BOTTOM
        assert len(rows) == 16 and all(len(r) == 16 for r in rows), rows
        out.append([[KEYS.index(c) for c in r] for r in rows])
    return out


def tiles16(pix):
    """A 16x16 picture as four 4bpp tiles, in the order of one 16x16 object."""
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
    gfx = b''.join(b''.join(tiles16(p)) for p in pics)
    with open(os.path.join(GEN, 'coop', 'revive_fairy.4bpp'), 'wb') as f:
        f.write(gfx)
    with open(os.path.join(GEN, 'coop', 'revive_fairy.inc'), 'w') as f:
        f.write('@ Made by blend/coop/revive_fairy.py: the Revive Fairy.\n')
        f.write('\t.align 2\ngSprite_ReviveFairy::\n\t.incbin "custom/gen/coop/revive_fairy.4bpp"\n\n')
        f.write('\t.align 2\ngSpriteFrames_ReviveFairy::\n')
        for i in range(len(pics)):
            f.write('\t.byte 4, 0\n\t.2byte %d\n' % (i * 4))
        # animation 0: the jar, looping; animation 1: the first frame held (the HUD)
        f.write('\ngSpriteAnimations_ReviveFairy_0:\n')
        for i, d in enumerate(DURATIONS):
            f.write('\t.byte %d, %d, 0, 0x%x\n' % (i, d, 0x80 if i == len(pics) - 1 else 0))
        f.write('\t.byte %d, 0, 0, 0\n' % len(pics))
        f.write('gSpriteAnimations_ReviveFairy_1:\n\t.byte 0, 255, 0, 0x80\n\t.byte 1, 0, 0, 0\n')
        f.write('\n\t.align 2\ngSpriteAnimations_ReviveFairy::\n')
        f.write('\t.4byte gSpriteAnimations_ReviveFairy_0\n\t.4byte gSpriteAnimations_ReviveFairy_1\n\t.4byte 0\n')
    with open(os.path.join(GEN, 'coop', 'revive_fairy.h'), 'w') as f:
        f.write('/* Made by blend/coop/revive_fairy.py */\n')
        f.write('static const u16 sReviveFairyPalette[16] = {\n    ')
        f.write(', '.join('0x%04x' % gba(c) for c in PALETTE))
        f.write(',\n};\n')
    with open(os.path.join(GEN, 'objlists', '%d.s' % SPRITE), 'w') as f:
        f.write('@ Made by blend/coop/revive_fairy.py\n\t.align 2\nBlendObjLists_%d:\n' % SPRITE)
        for i in range(len(pics)):
            f.write('\t.4byte BlendObjLists_%d_%d - gFrameObjLists\n' % (SPRITE, i))
        for i in range(len(pics)):
            # one 16x16 object (shape 0, size 1), standing on the entity's position
            f.write('BlendObjLists_%d_%d:\n\t.byte 1\n\t.byte -8, -14, 0x10\n\t.2byte 0\n' % (SPRITE, i))


def write_host(pics):
    with open(HOST, 'w') as f:
        f.write('/* Made by blend/coop/revive_fairy.py: the Revive Fairy, 16x16 RGBA (0xAABBGGRR). */\n')
        f.write('#define REVIVE_ART_FRAMES %d\n' % len(pics))
        f.write('static const unsigned short sReviveArtDurations[] = { %s };\n' % ', '.join(map(str, DURATIONS)))
        f.write('static const unsigned int sReviveArt[%d][16 * 16] = {\n' % len(pics))
        for p in pics:
            f.write('    {\n')
            for row in p:
                vals = []
                for c in row:
                    if c == 0:
                        vals.append('0x00000000')
                    else:
                        r, g, b = PALETTE[c]
                        # what the GBA shows: 5 bits a channel
                        r, g, b = [(v >> 3) * 255 // 31 for v in (r, g, b)]
                        vals.append('0xff%02x%02x%02x' % (b, g, r))
                f.write('        ' + ', '.join(vals) + ',\n')
            f.write('    },\n')
        f.write('};\n')


def preview(pics, path):
    from PIL import Image, ImageDraw
    z = 12
    im = Image.new('RGB', (len(pics) * (16 * z + 24) + 24, 16 * z + 48), (58, 64, 72))
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
    write_host(pics)
    if '--preview' in sys.argv:
        preview(pics, sys.argv[sys.argv.index('--preview') + 1])
    print('Revive Fairy: %d frames (sprite %d)' % (len(pics), SPRITE))


if __name__ == '__main__':
    main()
