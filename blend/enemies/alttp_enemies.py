#!/usr/bin/env python3
"""
A Link to the Past's enemies for the blend, drawn from your own ALttP cartridge.

Each enemy's frames are built the way A Link to the Past builds them: from its OAM
pieces (the layouts below are zelda3's draw tables) over the sprite sheets the
dungeon loads. Frames are then recolored in the blend's Minish Cap lighting, and
extra frames the Minish Cap-style motion needs (squash and stretch on hops and
landings, idle wobbles) are made from the same pixels and added onto the sheet.

Writes, under tmc/custom/gen/ (git-ignored, since it holds graphics from the ROM):
  alttp_enemies.4bpp     every frame's tiles, one after the other
  alttp_enemies.inc      sprite frames, one-frame animations and palettes (asm)
  alttp_enemies.h        frame and palette numbers for src/enemy/alttpEnemies.c
  objlists/155.s         the frames' OAM layouts for gFrameObjLists (sprite 155)

  ./alttp_enemies.py path/to/alttp-usa.sfc [--preview out.png]

Needs zelda3's asset tools, which make-dungeons.sh unpacks into blend/out/z3.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
TMC = os.path.join(ROOT, 'tmc')
OUT = os.path.join(TMC, 'custom', 'gen')
Z3_ASSETS = os.environ.get('ZELDA3_ASSETS', os.path.join(ROOT, 'blend', 'out', 'z3', 'assets'))
sys.path.insert(0, os.path.join(ROOT, 'blend', 'dungeons'))

import blendstyle  # noqa: E402

SPRITE_INDEX = 155
DUNGEON_PALSET = 11  # the Eastern Palace's sprite colors

# ---------------------------------------------------------------------------
# sheets and colors from the cartridge

_z3 = None


def z3():
    global _z3
    if _z3 is None:
        sys.path.insert(0, Z3_ASSETS)
        import util
        import tables
        import sprite_sheets
        _z3 = (util, tables, sprite_sheets)
    return _z3


class Sheets:
    """The four sprite sheets one dungeon sprite set loads (OAM chars 0x00-0xff)."""

    def __init__(self, dset, overworld=False):
        util, tables, S = z3()
        self.sets = []
        for ss in range(4):
            ts = tables.kSpriteTilesets[dset + (0 if overworld else 64)][ss]
            self.sets.append(S.decode_3bit_tileset(ts))

    def pixel(self, char, x, y):
        """Color index (0-15, 0 or 8 = clear) of one pixel of an 8x8 char."""
        src = self.sets[char >> 6]
        c = char & 63
        return src[((c >> 4) * 8 + y) * 128 + (c & 15) * 8 + x]


def snes_palette(pal_base, palset=DUNGEON_PALSET, mode=2):
    """The 16 colors of one ALttP sprite palette (both halves), as RGB. mode: 0 Light
    World, 1 Dark World, 2 dungeon."""
    util, tables, S = z3()
    out = [(0, 0, 0)] * 16
    for hi in (0, 1):
        pal_idx = pal_base * 2 + hi
        try:
            sub = S.get_palette_subidx(palset, mode, pal_idx)
            words = S.get_palette_subset(pal_idx, sub)
        except AssertionError:
            continue
        for i, w in enumerate(words[:7]):
            r, g, b = w & 31, w >> 5 & 31, w >> 10 & 31
            out[hi * 8 + 1 + i] = (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2)
    return out


def mc_palette(pal_base, tint=None, palset=DUNGEON_PALSET, mode=2):
    """An ALttP palette re-lit the way the blend lights Minish Cap (see blendstyle)."""
    cols = snes_palette(pal_base, palset, mode)
    out = []
    for i, c in enumerate(cols):
        if i % 8 == 0:
            out.append((0, 0, 0))
            continue
        c = blendstyle.blend_color(c)
        if tint:
            c = blendstyle.recolor_toward(c, tint)
        out.append(c)
    return out


def gba_color(rgb):
    r, g, b = rgb
    return (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10


# ---------------------------------------------------------------------------
# frames

W, H = 64, 64          # work canvas
CX, CY = 32, 44        # the entity's position on it


class Frame:
    def __init__(self, name, pix):
        self.name = name
        self.pix = pix    # dict (x, y) -> color index, relative to the entity position


def compose(sheets, pieces, origin):
    """Build a frame from ALttP OAM pieces: (x, y, char, flags, big). The first piece
    is in front, as in ALttP. origin is the ALttP point that becomes the entity's
    position."""
    pix = {}
    for (x, y, char, flags, big) in reversed(pieces):
        size = 16 if big else 8
        for py in range(size):
            for px in range(size):
                c = char + (py >> 3) * 16 + (px >> 3)
                v = sheets.pixel(c, px & 7, py & 7)
                if v % 8 == 0:
                    continue
                dx = size - 1 - px if flags & 0x40 else px
                dy = size - 1 - py if flags & 0x80 else py
                pix[(x + dx - origin[0], y + dy - origin[1])] = v
    return pix


def bbox(pix):
    xs = [p[0] for p in pix]
    ys = [p[1] for p in pix]
    return min(xs), min(ys), max(xs), max(ys)


def scaled(pix, sx, sy, dy=0):
    """Squash or stretch a frame about the middle of its feet (Minish Cap motion)."""
    x0, y0, x1, y1 = bbox(pix)
    cx = (x0 + x1 + 1) / 2
    out = {}
    w = int(round((x1 - x0 + 1) * sx))
    h = int(round((y1 - y0 + 1) * sy))
    for ty in range(h):
        for tx in range(w):
            u = x0 + int((tx + 0.5) / sx)
            v = y1 - int((h - 1 - ty + 0.5) / sy)
            c = pix.get((u, v))
            if c:
                out[(int(round(cx - w / 2)) + tx, y1 - (h - 1 - ty) + dy)] = c
    return out


def shifted(pix, dx, dy):
    return {(x + dx, y + dy): c for (x, y), c in pix.items()}


def mirrored(pix):
    return {(-1 - x, y): c for (x, y), c in pix.items()}


def merged(*layers):
    """Layers drawn back to front."""
    out = {}
    for l in layers:
        out.update(l)
    return out


# ---------------------------------------------------------------------------
# A Link to the Past's OAM layouts (zelda3, sprite_main.c)

# Stalfos_Draw: 12 poses of (head, body, body); the head char follows the head's facing
STALFOS_DMD = [
    [(0, -10, 0x00, 0x00, 2), (0, 0, 0x06, 0x00, 2), (0, 0, 0x06, 0x00, 2)],
    [(0, -9, 0x00, 0x00, 2), (0, 1, 0x06, 0x40, 2), (0, 1, 0x06, 0x40, 2)],
    [(0, -10, 0x04, 0x00, 2), (0, 0, 0x06, 0x00, 2), (0, 0, 0x06, 0x00, 2)],
    [(0, -9, 0x04, 0x00, 2), (0, 1, 0x06, 0x40, 2), (0, 1, 0x06, 0x40, 2)],
    [(0, -10, 0x02, 0x00, 2), (5, 5, 0x2e, 0x00, 0), (0, 0, 0x24, 0x00, 2)],
    [(0, -10, 0x02, 0x00, 2), (0, 0, 0x0e, 0x00, 2), (0, 0, 0x0e, 0x00, 2)],
    [(0, -10, 0x02, 0x40, 2), (3, 5, 0x2e, 0x40, 0), (0, 0, 0x24, 0x40, 2)],
    [(0, -10, 0x02, 0x40, 2), (0, 0, 0x0e, 0x40, 2), (0, 0, 0x0e, 0x00, 2)],
    [(2, -8, 0x02, 0x40, 2), (0, 0, 0x08, 0x40, 2), (0, 0, 0x08, 0x40, 2)],
    [(-2, -8, 0x02, 0x00, 2), (0, 0, 0x08, 0x00, 2), (0, 0, 0x08, 0x00, 2)],
    [(0, -6, 0x00, 0x00, 2), (0, 0, 0x0a, 0x00, 2), (0, 0, 0x0a, 0x00, 2)],
    [(0, 0, 0x0a, 0x00, 2), (0, -6, 0x04, 0x00, 2), (0, -6, 0x04, 0x00, 2)],
]
# head char and flip by facing: right, left, down, up (ALttP's direction order)
STALFOS_HEAD = [(0x02, 0x40), (0x02, 0x00), (0x00, 0x00), (0x04, 0x00)]
# walking poses by facing (two steps each): kStalfos_AnimState1
STALFOS_WALK = [(6, 7), (4, 5), (0, 1), (2, 3)]
STALFOS_JUMP = [8, 9, 10, 11]

# Eyegore_Draw: 12 poses of four pieces
EYEGORE_DMD = [
    [(-4, -4, 0xa2, 0x00, 2), (4, -4, 0xa2, 0x40, 2), (-4, 4, 0x9c, 0x00, 2), (4, 4, 0x9c, 0x40, 2)],
    [(-4, -4, 0xa4, 0x00, 2), (4, -4, 0xa4, 0x40, 2), (-4, 4, 0x9c, 0x00, 2), (4, 4, 0x9c, 0x40, 2)],
    [(-4, -4, 0x8c, 0x00, 2), (4, -4, 0x8c, 0x40, 2), (-4, 4, 0x9c, 0x00, 2), (4, 4, 0x9c, 0x40, 2)],
    [(-4, -3, 0x8c, 0x00, 2), (12, -3, 0x8c, 0x40, 0), (-4, 13, 0xbc, 0x00, 0), (4, 5, 0x8a, 0x40, 2)],
    [(-4, -3, 0x8c, 0x00, 0), (4, -3, 0x8c, 0x40, 2), (-4, 5, 0x8a, 0x00, 2), (12, 13, 0xbc, 0x40, 0)],
    [(0, -4, 0xaa, 0x00, 2), (0, 4, 0xa6, 0x00, 2), (0, -4, 0xaa, 0x00, 2), (0, 4, 0xa6, 0x00, 2)],
    [(0, -3, 0xaa, 0x00, 2), (0, 4, 0xa8, 0x00, 2), (0, -3, 0xaa, 0x00, 2), (0, 4, 0xa8, 0x00, 2)],
    [(0, -4, 0xaa, 0x40, 2), (0, 4, 0xa6, 0x40, 2), (0, -4, 0xaa, 0x40, 2), (0, 4, 0xa6, 0x40, 2)],
    [(0, -3, 0xaa, 0x40, 2), (0, 4, 0xa8, 0x40, 2), (0, -3, 0xaa, 0x40, 2), (0, 4, 0xa8, 0x40, 2)],
    [(-4, -4, 0x8e, 0x00, 2), (4, -4, 0x8e, 0x40, 2), (-4, 4, 0x9e, 0x00, 2), (4, 4, 0x9e, 0x40, 2)],
    [(-4, -3, 0x8e, 0x00, 2), (12, -3, 0x8e, 0x40, 0), (-4, 13, 0xbd, 0x00, 0), (4, 5, 0xa0, 0x40, 2)],
    [(-4, -3, 0x8e, 0x00, 0), (4, -3, 0x8e, 0x40, 2), (-4, 5, 0xa0, 0x00, 2), (12, 13, 0xbd, 0x40, 0)],
]

# Antifairy (SpriteDraw_Antfairy): a core and four sparks circling it
ANTIFAIRY_DMD = [
    [(4, 2, 0xe1, 0, 0), (4, -3, 0xe3, 0, 0), (-1, 2, 0xe3, 0, 0), (9, 2, 0xe3, 0, 0), (4, 7, 0xe3, 0, 0)],
    [(4, 2, 0xe1, 0, 0), (3, -3, 0xe3, 0, 0), (9, 1, 0xe3, 0, 0), (-1, 3, 0xe3, 0, 0), (5, 7, 0xe3, 0, 0)],
    [(4, 2, 0xe1, 0, 0), (1, -3, 0xe3, 0, 0), (9, -1, 0xe3, 0, 0), (-1, 5, 0xe3, 0, 0), (7, 7, 0xe3, 0, 0)],
    [(4, 2, 0xe1, 0, 0), (0, -2, 0xe3, 0, 0), (8, -2, 0xe3, 0, 0), (0, 6, 0xe3, 0, 0), (8, 6, 0xe3, 0, 0)],
    [(4, 2, 0xe1, 0, 0), (7, -3, 0xe3, 0, 0), (-1, -1, 0xe3, 0, 0), (9, 5, 0xe3, 0, 0), (1, 7, 0xe3, 0, 0)],
    [(4, 2, 0xe1, 0, 0), (5, -3, 0xe3, 0, 0), (-1, 1, 0xe3, 0, 0), (9, 3, 0xe3, 0, 0), (3, 7, 0xe3, 0, 0)],
]

# ArmosKnight_Draw: whole (pose 0) and five poses of the knight bursting apart
_AK_X = [-8, 8, -8, 8, -10, 10, -10, 10, -10, 10, -10, 10, -12, 12, -12, 12, -14, 14, -14, 14, -16, 24, -16, 24]
_AK_Y = [-8, -8, 8, 8, -10, -10, 10, 10, -10, -10, 10, 10, -12, -12, 12, 12, -14, -14, 14, 14, -16, -16, 24, 24]
_AK_C = [0xc0, 0xc2, 0xe0, 0xe2, 0xc0, 0xc2, 0xe0, 0xe2, 0xc4, 0xc4, 0xc4, 0xc4, 0xc6, 0xc6, 0xc6, 0xc6,
         0xc8, 0xc8, 0xc8, 0xc8, 0xd8, 0xd8, 0xd8, 0xd8]
_AK_F = [0, 0, 0, 0, 0, 0, 0, 0, 0x40, 0, 0xc0, 0x80, 0x40, 0, 0xc0, 0x80, 0x40, 0, 0xc0, 0x80, 0x40, 0, 0xc0, 0x80]
_AK_B = [2] * 20 + [0] * 4
ARMOS_DMD = [[(_AK_X[g * 4 + i], _AK_Y[g * 4 + i], _AK_C[g * 4 + i], _AK_F[g * 4 + i], _AK_B[g * 4 + i])
              for i in range(4)] for g in range(6)]

# Rope: one 16x16 char per pose (SpriteDraw_SingleLarge), by facing two steps each
ROPE_CHARS = [0xa4, 0xa6, 0xa8, 0xaa]
ROPE_POSES = [[(2, 0x40), (3, 0x40)], [(2, 0), (3, 0)], [(0, 0), (0, 0x40)], [(1, 0), (1, 0x40)]]

# Moblin_Draw: 12 poses of four pieces (two 8x8 spear halves, head, body)
_MOBLIN = [
    (-2, 3, 0x91, 0x80, 0), (-2, 11, 0x90, 0x80, 0), (0, -10, 0x86, 0x00, 2), (0, 0, 0x8a, 0x00, 2),
    (-2, 7, 0x91, 0x80, 0), (-2, 15, 0x90, 0x80, 0), (0, -10, 0x86, 0x00, 2), (0, 0, 0x8a, 0x40, 2),
    (0, -9, 0x84, 0x00, 2), (0, 0, 0xa0, 0x00, 2), (11, -5, 0x90, 0x00, 0), (11, 3, 0x91, 0x00, 0),
    (0, -9, 0x84, 0x00, 2), (0, 0, 0xa0, 0x40, 2), (11, -8, 0x90, 0x00, 0), (11, 0, 0x91, 0x00, 0),
    (-4, 8, 0x80, 0x00, 0), (4, 8, 0x81, 0x00, 0), (0, -9, 0x88, 0x00, 2), (0, 0, 0xa6, 0x00, 2),
    (-9, 6, 0x80, 0x00, 0), (-1, 6, 0x81, 0x00, 0), (0, -8, 0x88, 0x00, 2), (0, 0, 0xa4, 0x00, 2),
    (12, 8, 0x80, 0x40, 0), (4, 8, 0x81, 0x40, 0), (0, -9, 0x88, 0x40, 2), (0, 0, 0xa6, 0x40, 2),
    (17, 6, 0x80, 0x40, 0), (9, 6, 0x81, 0x40, 0), (0, -8, 0x88, 0x40, 2), (0, 0, 0xa4, 0x40, 2),
    (-3, -5, 0x91, 0x80, 0), (-3, 3, 0x90, 0x80, 0), (0, -10, 0x86, 0x00, 2), (0, 0, 0xa8, 0x00, 2),
    (11, -11, 0x90, 0x00, 0), (11, -3, 0x91, 0x00, 0), (0, -9, 0x84, 0x00, 2), (0, 0, 0x82, 0x40, 2),
    (-2, -3, 0x80, 0x00, 0), (6, -3, 0x81, 0x00, 0), (0, -9, 0x88, 0x00, 2), (0, 0, 0xa2, 0x00, 2),
    (10, -3, 0x80, 0x40, 0), (2, -3, 0x81, 0x40, 0), (0, -9, 0x88, 0x40, 2), (0, 0, 0xa2, 0x40, 2),
]
MOBLIN_DMD = [_MOBLIN[g * 4:g * 4 + 4] for g in range(12)]
# the thrown spear, by facing: right, left, down, up
SPEAR_DMD = [[(8, 0, 0x80, 0x40, 0), (0, 0, 0x81, 0x40, 0)], [(0, 0, 0x80, 0, 0), (8, 0, 0x81, 0, 0)],
             [(0, 8, 0x90, 0x80, 0), (0, 0, 0x91, 0x80, 0)], [(0, 0, 0x90, 0, 0), (0, 8, 0x91, 0, 0)]]

# Popo (Bot_Draw): one 16x16 char, two poses, mirrored on alternate beats
POPO_CHARS = [0x40, 0x42]


# ---------------------------------------------------------------------------
# the enemies

PALETTES = []      # (name, 16 RGB colors)
FRAMES = []        # Frame


def palette(name, cols):
    PALETTES.append((name, cols))


def frame(name, pix):
    FRAMES.append(Frame(name, pix))


def build_frames():
    eastern = Sheets(8)     # the Eastern Palace's sprite set
    armos = Sheets(9)       # the Armos Knights' room

    # --- Stalfos: walk (each facing, two steps) with the head looking any way,
    # hops (ALttP's jump pose, plus a crouch and a landing squash added for
    # Minish Cap-style weight), and a collapse into a pile of bones.
    org = (8, 12)
    for face in range(4):
        for step in range(2):
            for head in range(4):
                pieces = [list(p) for p in STALFOS_DMD[STALFOS_WALK[face][step]]]
                pieces[0][2], pieces[0][3] = STALFOS_HEAD[head]
                frame('STALFOS_WALK_%d_%d_%d' % (face, step, head), compose(eastern, pieces, org))
    for face in range(4):
        jump = compose(eastern, STALFOS_DMD[STALFOS_JUMP[face]], org)
        stand = compose(eastern, STALFOS_DMD[STALFOS_WALK[face][0]], org)
        frame('STALFOS_JUMP_%d' % face, scaled(jump, 0.9, 1.1))
        frame('STALFOS_CROUCH_%d' % face, scaled(stand, 1.15, 0.8))
        frame('STALFOS_LAND_%d' % face, scaled(jump, 1.2, 0.75))
    # bones: the body squashed flat with the skull resting on it
    body = compose(eastern, [(0, 0, 0x0a, 0, 2)], org)
    skull = compose(eastern, [(0, 0, 0x00, 0, 2)], org)
    frame('STALFOS_BONES', merged(scaled(body, 1.25, 0.45), shifted(scaled(skull, 0.9, 0.9), 3, -2)))

    # --- Eyegore: the eye opening (closed, half, open), then walking each way
    org = (4, 12)
    for g in range(12):
        frame('EYEGORE_%d' % g, compose(eastern, EYEGORE_DMD[g], org))
    # an added stomp: the closed statue settling after a step
    frame('EYEGORE_SETTLE', scaled(compose(eastern, EYEGORE_DMD[0], org), 1.08, 0.92))

    # --- Popo: ALttP's two poses and their mirrors, plus a squash and a stretch
    # for a Minish Cap-style wobble
    org = (8, 12)
    for i, c in enumerate(POPO_CHARS):
        p = compose(eastern, [(0, 0, c, 0, 2)], org)
        frame('POPO_%d' % i, p)
        frame('POPO_%d_FLIP' % i, mirrored(p))
    base = compose(eastern, [(0, 0, POPO_CHARS[0], 0, 2)], org)
    frame('POPO_SQUASH', scaled(base, 1.2, 0.8))
    frame('POPO_STRETCH', scaled(base, 0.85, 1.2))

    # --- Antifairy (Bubble): the sparks circling the core
    org = (8, 8)
    for g in range(6):
        frame('ANTIFAIRY_%d' % g, compose(eastern, ANTIFAIRY_DMD[g], org))

    # --- Armos Knight: whole, squashed for a landing and stretched for a takeoff,
    # and the five poses of it bursting apart
    org = (8, 20)
    whole = compose(armos, ARMOS_DMD[0], org)
    frame('ARMOS_0', whole)
    frame('ARMOS_SQUASH', scaled(whole, 1.12, 0.84))
    frame('ARMOS_STRETCH', scaled(whole, 0.9, 1.1))
    frame('ARMOS_TILT_L', shifted(whole, -1, 0))
    frame('ARMOS_TILT_R', shifted(whole, 1, 0))
    for g in range(1, 6):
        frame('ARMOS_BURST_%d' % g, compose(armos, ARMOS_DMD[g], org))

    # --- Rope (a Dark World snake): two poses each way, plus a coiled squash for the
    # moment before it lunges
    rope = Sheets(1)
    org = (8, 12)
    for face in range(4):
        for step in range(2):
            g, fl = ROPE_POSES[face][step]
            frame('ROPE_%d_%d' % (face, step), compose(rope, [(0, 0, ROPE_CHARS[g], fl, 2)], org))
        g, fl = ROPE_POSES[face][0]
        frame('ROPE_COIL_%d' % face, scaled(compose(rope, [(0, 0, ROPE_CHARS[g], fl, 2)], org), 1.15, 0.8))

    # --- Moblin: walking each way with its spear, winding up and throwing, the same
    # poses empty-handed while the spear flies, and a lean into the throw
    moblin = Sheets(19, overworld=True)
    org = (8, 12)
    for g in range(12):
        frame('MOBLIN_%d' % g, compose(moblin, MOBLIN_DMD[g], org))
    for g in range(12):
        frame('MOBLIN_BARE_%d' % g, compose(moblin, [p for p in MOBLIN_DMD[g] if p[4]], org))
    for face in range(4):
        frame('SPEAR_%d' % face, compose(moblin, SPEAR_DMD[face], (8, 4) if face < 2 else (4, 8)))

    palette('ROPE', mc_palette(6, palset=1))
    palette('MOBLIN', mc_palette(4, palset=14, mode=0))
    palette('STALFOS', mc_palette(4))
    palette('EYEGORE_GREEN', mc_palette(6))
    palette('EYEGORE_RED', mc_palette(3))
    palette('POPO', mc_palette(4))
    palette('ANTIFAIRY', mc_palette(1))
    palette('ARMOS', mc_palette(4))
    palette('ARMOS_RED', mc_palette(3))


# ---------------------------------------------------------------------------
# Minish Cap sprite data

# object (width, height) -> (shape, size)
SHAPES = {(8, 8): (0, 0), (16, 16): (0, 1), (32, 32): (0, 2), (16, 8): (1, 0), (32, 8): (1, 1),
          (32, 16): (1, 2), (8, 16): (2, 0), (8, 32): (2, 1), (16, 32): (2, 2)}


def cut(pix):
    """Cover a frame with OAM objects: a grid of 16x16 cells from its top left, each
    shrunk to the half it uses. A frame that fills a 32x32 square is one object."""
    x0, y0, x1, y1 = bbox(pix)
    objs = []
    if x1 - x0 < 32 and y1 - y0 < 32 and len(pix) > 400:
        return [(x0, y0, 32, 32)]
    for gy in range(y0, y1 + 1, 16):
        for gx in range(x0, x1 + 1, 16):
            used = [(x, y) for (x, y) in pix if gx <= x < gx + 16 and gy <= y < gy + 16]
            if not used:
                continue
            left = any(x < gx + 8 for x, y in used)
            right = any(x >= gx + 8 for x, y in used)
            top = any(y < gy + 8 for x, y in used)
            bottom = any(y >= gy + 8 for x, y in used)
            w = 16 if left and right else 8
            h = 16 if top and bottom else 8
            ox = gx if left else gx + 8
            oy = gy if top else gy + 8
            objs.append((ox, oy, w, h))
    return objs


def tiles_of(pix, ox, oy, w, h):
    out = []
    for ty in range(h // 8):
        for tx in range(w // 8):
            t = bytearray(32)
            for y in range(8):
                for x in range(8):
                    v = pix.get((ox + tx * 8 + x, oy + ty * 8 + y), 0)
                    if x & 1:
                        t[y * 4 + x // 2] |= v << 4
                    else:
                        t[y * 4 + x // 2] |= v
            out.append(bytes(t))
    return out


def write(preview=None):
    os.makedirs(os.path.join(OUT, 'objlists'), exist_ok=True)
    gfx = bytearray()
    frames_asm = []
    objs_asm = ['@ generated by blend/enemies/alttp_enemies.py from your own cartridge; do not commit',
                '@ OAM layouts of sprite %d (A Link to the Past enemies)' % SPRITE_INDEX,
                '\t.align 2',
                'BlendObjLists_%d::' % SPRITE_INDEX]
    objs_body = []
    max_tiles = 0
    for n, f in enumerate(FRAMES):
        first = len(gfx) // 32
        objs = cut(f.pix)
        ntiles = 0
        lines = []
        for (ox, oy, w, h) in objs:
            shape, size = SHAPES[(w, h)]
            ts = tiles_of(f.pix, ox, oy, w, h)
            lines.append('\t.byte %d, %d, 0x%02x\n\t.2byte %d' % (ox & 0xff, oy & 0xff,
                                                                    shape << 6 | size << 4, ntiles))
            gfx += b''.join(ts)
            ntiles += len(ts)
        assert ntiles <= 64, f.name
        max_tiles = max(max_tiles, ntiles)
        f.ntiles = ntiles
        frames_asm.append('\tsprite_frame first_tile_index=0x%x, num_tiles=%d @ %s' % (first, ntiles, f.name))
        objs_asm.append('\t.4byte BlendObj_%d_%d - gFrameObjLists' % (SPRITE_INDEX, n))
        objs_body.append('BlendObj_%d_%d: @ %s\n\t.byte %d\n%s' % (SPRITE_INDEX, n, f.name, len(objs), '\n'.join(lines)))
    assert len(FRAMES) < 255
    objs_asm += objs_body

    with open(os.path.join(OUT, 'alttp_enemies.4bpp'), 'wb') as fh:
        fh.write(gfx)
    with open(os.path.join(OUT, 'objlists', '%d.s' % SPRITE_INDEX), 'w') as fh:
        fh.write('\n'.join(objs_asm) + '\n')

    inc = ['@ generated by blend/enemies/alttp_enemies.py from your own cartridge; do not commit',
           '\t.align 2',
           'gSprite_AlttpEnemies::',
           '\t.incbin "custom/gen/alttp_enemies.4bpp"',
           '\t.align 2',
           'gSpriteFrames_AlttpEnemies::'] + frames_asm
    inc += ['\t.align 2', 'gSpriteAnimations_AlttpEnemies::']
    inc += ['\t.4byte AlttpEnemyAnim_%d' % n for n in range(len(FRAMES))]
    for n in range(len(FRAMES)):
        # one frame held, looping onto itself; the enemy code picks frames itself
        inc.append('AlttpEnemyAnim_%d:\n\tkeyframe frame_index=%d, duration=255, bitfield2=0x80\n\t.byte 1' % (n, n))
    inc += ['\t.align 2', 'gAlttpEnemyPalettes::']
    for name, cols in PALETTES:
        inc.append('\t.2byte ' + ', '.join('0x%04x' % gba_color(c) for c in cols) + ' @ ' + name)
    with open(os.path.join(OUT, 'alttp_enemies.inc'), 'w') as fh:
        fh.write('\n'.join(inc) + '\n')

    h = ['/* generated by blend/enemies/alttp_enemies.py; frame and palette numbers */',
         '#ifndef ALTTP_ENEMIES_GEN_H', '#define ALTTP_ENEMIES_GEN_H',
         '#define ALTTP_ENEMY_SPRITE %d' % SPRITE_INDEX,
         '#define ALTTP_FRAME_MAX_TILES %d' % max_tiles]
    for n, f in enumerate(FRAMES):
        h.append('#define AF_%s %d' % (f.name, n))
    slots = {}
    for f in FRAMES:
        kind = f.name.split('_')[0]
        slots[kind] = max(slots.get(kind, 1), (f.ntiles + 15) // 16)
    for kind, n in sorted(slots.items()):
        h.append('#define AS_%s %d /* 16-tile gfx slots its largest frame needs */' % (kind, n))
    for n, (name, cols) in enumerate(PALETTES):
        h.append('#define AP_%s %d' % (name, n))
    h.append('#endif')
    with open(os.path.join(OUT, 'alttp_enemies.h'), 'w') as fh:
        fh.write('\n'.join(h) + '\n')
    print('alttp_enemies: %d frames, %d tiles, %d palettes (largest frame %d tiles)' % (
        len(FRAMES), len(gfx) // 32, len(PALETTES), max_tiles))

    if preview:
        draw_preview(preview)


def draw_preview(path):
    """Every frame on a sheet, for checking by eye (stays out of git: it is ROM art)."""
    from PIL import Image
    pal_for = {'STALFOS': 'STALFOS', 'EYEGORE': 'EYEGORE_GREEN', 'POPO': 'POPO',
               'ANTIFAIRY': 'ANTIFAIRY', 'ARMOS': 'ARMOS', 'ROPE': 'ROPE', 'MOBLIN': 'MOBLIN',
               'SPEAR': 'MOBLIN'}
    pals = dict(PALETTES)
    cols = 12
    rows = (len(FRAMES) + cols - 1) // cols
    im = Image.new('RGB', (cols * 48, rows * 56), (40, 48, 40))
    for n, f in enumerate(FRAMES):
        pal = pals[pal_for[f.name.split('_')[0]]]
        bx, by = (n % cols) * 48 + 24, (n // cols) * 56 + 40
        for (x, y), c in f.pix.items():
            if 0 <= bx + x < im.width and 0 <= by + y < im.height:
                im.putpixel((bx + x, by + y), pal[c])
        im.putpixel((bx, by), (255, 0, 0))
    im = im.resize((im.width * 3, im.height * 3), Image.NEAREST)
    im.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom', help='your A Link to the Past (USA) cartridge')
    ap.add_argument('--preview', help='also draw every frame to this PNG')
    a = ap.parse_args()
    util, tables, S = z3()
    util.load_rom(a.rom)
    build_frames()
    write(a.preview)


if __name__ == '__main__':
    main()
