#!/usr/bin/env python3
"""
A Link to the Past's enemies for the blend, drawn from your own ALttP cartridge.

Each enemy's frames are built the way A Link to the Past builds them: from its OAM
pieces (the layouts below are zelda3's draw tables) over the sprite sheets the
dungeon loads. Frames are then recolored in the blend's Minish Cap lighting, and
extra frames the Minish Cap-style motion needs (squash and stretch on hops and
landings, idle wobbles) are made from the same pixels and added onto the sheet.

Writes, under tmc/custom/gen/ (git-ignored, since it holds graphics from the ROM):
  alttp_enemies.4bpp     sprite 155's tiles (Eastern Palace, Dark World), frame after frame
  alttp_enemies2.4bpp    sprite 329's tiles (Desert Palace, Tower of Hera, Hinox)
  alttp_enemies.inc      both sprites' frames and one-frame animations, and the palettes
  alttp_enemies.h        frame and palette numbers for src/enemy/alttpEnemies.c
  objlists/155.s, 329.s  the frames' OAM layouts for gFrameObjLists

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

SPRITE_INDEX = 155    # the Eastern Palace and Dark World set
SPRITE_INDEX_2 = 329  # the Desert Palace, Tower of Hera and Hinox set (a new gSpritePtrs entry)
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


class MiscSheet:
    """The shared sheet loaded into name table 0, chars 0x40-0x7f (bombs, sparks)."""

    def __init__(self, ts=10):
        util, tables, S = z3()
        self.src = S.decode_3bit_tileset(ts)

    def pixel(self, char, x, y):
        c = char & 63
        return self.src[((c >> 4) * 8 + y) * 128 + (c & 15) * 8 + x]


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
    for piece in reversed(pieces):
        x, y, char, flags, big = piece[:5]
        add = piece[5] if len(piece) > 5 else 0
        size = 16 if big else 8
        for py in range(size):
            for px in range(size):
                c = char + (py >> 3) * 16 + (px >> 3)
                v = sheets.pixel(c, px & 7, py & 7)
                if v % 8 == 0:
                    continue
                dx = size - 1 - px if flags & 0x40 else px
                dy = size - 1 - py if flags & 0x80 else py
                pix[(x + dx - origin[0], y + dy - origin[1])] = v + add
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



# --- the Desert Palace, Tower of Hera and Hinox set (sprite 329) -----------

# Hinox_Draw: 12 poses; the bomb it holds (name table 0) is left out, since the
# Hinox throws a Minish Cap bomb
_HINOX = [
    (0, -13, 0x0600, 2), (-8, -5, 0x0624, 2), (8, -5, 0x4624, 2), (0, 1, 0x0606, 2),
    (0, -13, 0x0600, 2), (-8, -5, 0x0624, 2), (8, -5, 0x4624, 2), (0, 1, 0x4606, 2),
    (-8, -6, 0x0624, 2), (8, -6, 0x4624, 2), (0, 0, 0x0606, 2), (0, -13, 0x0604, 2),
    (-8, -6, 0x0624, 2), (8, -6, 0x4624, 2), (0, 0, 0x4606, 2), (0, -13, 0x0604, 2),
    (-3, -13, 0x0602, 2), (0, -8, 0x060c, 2), (0, 0, 0x061c, 2),
    (-3, -12, 0x0602, 2), (0, -8, 0x060e, 2), (0, 0, 0x061e, 2),
    (3, -13, 0x4602, 2), (0, -8, 0x460c, 2), (0, 0, 0x461c, 2),
    (3, -12, 0x4602, 2), (0, -8, 0x460e, 2), (0, 0, 0x461e, 2),
    (-13, -16, 0x056e, 2), (0, -13, 0x0600, 2), (-8, -5, 0x0620, 2), (8, -5, 0x4624, 2), (0, 1, 0x0606, 2),
    (-8, -5, 0x0624, 2), (8, -5, 0x4620, 2), (0, 1, 0x0606, 2), (0, -13, 0x0604, 2), (13, -16, 0x056e, 2),
    (-8, -11, 0x056e, 2), (-3, -13, 0x0602, 2), (0, 0, 0x0622, 2), (0, -8, 0x060c, 2),
    (8, -11, 0x056e, 2), (3, -13, 0x4602, 2), (0, 0, 0x4622, 2), (0, -8, 0x460c, 2),
]
_HINOX_NUM = [4, 4, 4, 4, 3, 3, 3, 3, 5, 5, 4, 4]
_HINOX_OFFS = [0, 4, 8, 12, 16, 19, 22, 25, 28, 33, 38, 42]
HINOX_DMD = [_HINOX[o:o + n] for o, n in zip(_HINOX_OFFS, _HINOX_NUM)]

# Lanmola_Draw: head and tail chars and flips by the angle it travels (0 right, 4 down)
LANMOLA_HEAD = [0xc4, 0xe2, 0xc2, 0xe0, 0xc0, 0xe0, 0xc2, 0xe2, 0xc4, 0xe2, 0xc2, 0xe0, 0xc0, 0xe0, 0xc2, 0xe2]
LANMOLA_TAIL = [0xcc, 0xe4, 0xca, 0xe6, 0xc8, 0xe6, 0xca, 0xe4, 0xcc, 0xe4, 0xca, 0xe6, 0xc8, 0xe6, 0xca, 0xe4]
LANMOLA_FLAGS = [0xc0] * 4 + [0x80] * 4 + [0] * 4 + [0x40] * 4
LANMOLA_MOUND = [(0xee, 0), (0xee, 0x40), (0xec, 0), (0xec, 0x40), (0xce, 0), (0xce, 0x40)]
_LM_X4 = [-8, 8, -10, 10, -16, 16, -24, 32]
_LM_Y4 = [0, 0, -1, -1, -1, -1, 3, 3]
_LM_C4 = [0xe8, 0xe8, 0xe8, 0xe8, 0xea, 0xea, 0xea, 0xea]
_LM_F4 = [0, 0x40] * 4
_LM_B4 = [2, 2, 2, 2, 2, 2, 0, 0]
LANMOLA_SPRAY = [[(_LM_X4[j], _LM_Y4[j], _LM_C4[j], _LM_F4[j], _LM_B4[j]) for j in (r + 1, r)] for r in (0, 2, 4, 6)]

# Leever_Draw: 13 poses, rising out of the sand and spinning
_LV_NUM = [1, 1, 1, 3, 3, 3, 3, 3, 3, 1, 1, 1, 1, 1]
_LV_X = [2, 6, 6, 6, 0, 8, 8, 8, 0, 8, 8, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0, 0, 8,
         0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
_LV_Y = [8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 5, 5, 8, 8, 5, 5, 8, 8, 2, 2, 8, 8, 1, 1, 8, 8, 0, 0, 8, 8,
         -1, -1, 8, 8, 8, -2, -2, 0, 8, -2, -2, 0, 8, -2, -2, 0, 8, -2, -2, 0, 8, -2, -2, 0]
_LV_C = [0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x38, 0x38, 0x38, 0x38, 8, 9, 0x28, 0x28,
         8, 9, 0xd9, 0xd9, 8, 8, 0xd8, 0xd8, 8, 8, 0xda, 0xda, 6, 6, 0xd9, 0xd9,
         0x26, 0x26, 0xd8, 0xd8, 0x6c, 6, 6, 0, 0x6c, 0x26, 0x26, 0, 0x6c, 6, 6, 0, 0x6c, 0x26, 0x26, 0, 0x6c, 8, 8, 0]
_LV_F = [1, 0x41, 0x41, 0x41, 1, 0x41, 0x41, 0x41, 1, 0x41, 0x41, 0x41, 1, 1, 1, 0x41,
         1, 1, 0, 0x40, 1, 1, 0, 0x40, 1, 1, 0, 0x40, 1, 1, 0, 0x40,
         0, 1, 0, 0x40, 6, 0x41, 0x41, 0, 6, 0x41, 0x41, 0, 6, 1, 1, 0, 6, 1, 1, 0, 6, 1, 1, 0]
_LV_B = [0] * 20 + [2, 2, 0, 0] * 4 + [2, 2, 2, 0] * 5


def leever_pieces(d):
    out = []
    for i in range(_LV_NUM[d], -1, -1):
        j = d * 4 + i
        c, f = _LV_C[j], _LV_F[j]
        if c >= 0x60 and not f & 1:
            continue  # sand spray and shadow from the shared item sheet
        out.append((_LV_X[j], _LV_Y[j], c, f & 0xc0, _LV_B[j]))
    return out


# Beamos: a dome on a pedestal, its eye sweeping round it (32 steps)
BEAMOS_EYE_X = [-1, 0, 1, 2, 3, 4, 5, 7, 8, 10, 11, 12, 13, 14, 15, 16,
                17, 15, 14, 13, 12, 11, 10, 8, 7, 5, 4, 3, 2, 1, 0, -2]
BEAMOS_EYE_Y = [11, 12, 13, 14, 14, 15, 15, 15, 15, 15, 15, 14, 14, 13, 12, 11,
                10, 9, 8, 7, 7, 6, 6, 6, 6, 6, 6, 7, 7, 8, 9, 10]
BEAMOS_EYE_C = [0x5b, 0x5b, 0x5a, 0x5a, 0x4b, 0x4b, 0x4a, 0x4a, 0x4a, 0x4a, 0x4b, 0x4b, 0x5a, 0x5a, 0x5b, 0x5b,
                0x5b, 0x5b, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x5b, 0x5b, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c]
BEAMOS_EYE_F = [0] * 8 + [0x40] * 16 + [0] * 8

# Gibdo_Draw: 12 poses of head and body
_GIBDO = [(0, -9, 0x0080, 2), (0, 0, 0x008a, 2), (0, -8, 0x0080, 2), (0, 1, 0x408a, 2),
          (0, -9, 0x0082, 2), (0, 0, 0x008c, 2), (0, -8, 0x0082, 2), (0, 0, 0x008e, 2),
          (0, -9, 0x0084, 2), (0, 0, 0x00a0, 2), (0, -8, 0x0084, 2), (0, 1, 0x40a0, 2),
          (0, -9, 0x0086, 2), (0, 0, 0x00a2, 2), (0, -9, 0x0088, 2), (0, 0, 0x00a4, 2),
          (0, -9, 0x4088, 2), (0, 0, 0x40a4, 2), (0, -9, 0x4082, 2), (0, 0, 0x408c, 2),
          (0, -9, 0x4086, 2), (0, 0, 0x40a2, 2), (0, -8, 0x4082, 2), (0, 1, 0x408e, 2)]
GIBDO_DMD = [_GIBDO[g * 2:g * 2 + 2] for g in range(12)]

# Giant Moldorm: a 32x32 head ringed by two eyes that follow its heading, two big
# segments, a small one and the tail (its weak spot)
MOLDORM_HEAD = [(-8, -8, 0x0080, 2), (8, -8, 0x0082, 2), (-8, 8, 0x00a0, 2), (8, 8, 0x00a2, 2)]
MOLDORM_SEG = [[(-8, -8, 0x0084, 2), (8, -8, 0x0086, 2), (-8, 8, 0x00a4, 2), (8, 8, 0x00a6, 2)],
               [(-8, -8, 0x4086, 2), (8, -8, 0x4084, 2), (-8, 8, 0x40a6, 2), (8, 8, 0x40a4, 2)]]
MOLDORM_EYE_X = [16, 15, 12, 6, 0, -6, -12, -13, -16, -13, -12, -6, 0, 6, 12, 15]
MOLDORM_EYE_Y = [0, 6, 12, 15, 16, 15, 12, 6, 0, -6, -12, -13, -16, -13, -12, -6]
MOLDORM_EYE_C = [0xaa, 0xaa, 0xa8, 0xa8, 0x8a, 0x8a, 0xa8, 0xa8, 0xaa, 0xaa, 0xa8, 0xa8, 0x8a, 0x8a, 0xa8, 0xa8]
MOLDORM_EYE_F = [0, 0, 0, 0, 0x80, 0x80, 0x40, 0x40, 0x40, 0x40, 0xc0, 0xc0, 0, 0, 0x80, 0x80]
MOLDORM_SPIN = [0, 0x40, 0xc0, 0x80]

# Bari (RedBari_Draw and the single-char poses): a jellyfish of four 8x8 quarters
BARI_DMD = [[(0, 0, 0x22, 0, 0), (8, 0, 0x22, 0x40, 0), (0, 8, 0x32, 0, 0), (8, 8, 0x32, 0x40, 0)],
            [(0, 0, 0x23, 0, 0), (8, 0, 0x23, 0x40, 0), (0, 8, 0x33, 0, 0), (8, 8, 0x33, 0x40, 0)]]

# HardHatBeetle_Draw (its own name-table bit is 0)
HARDHAT_DMD = [[(0, -4, 0x0140, 2), (0, 2, 0x0142, 2)], [(0, -5, 0x0140, 2), (0, 2, 0x0144, 2)]]

# Bumper_Draw: one quarter mirrored four ways, and the same pulled in by a pixel
BUMPER_DMD = [[(-8, -8, 0x00ec, 2), (8, -8, 0x40ec, 2), (-8, 8, 0x80ec, 2), (8, 8, 0xc0ec, 2)],
              [(-7, -7, 0x00ec, 2), (7, -7, 0x40ec, 2), (-7, 7, 0x80ec, 2), (7, 7, 0xc0ec, 2)]]

# preview colors for the kinds whose palette is not named after them
PREVIEW_PALETTES = {'LMDIRT': 'LANMOLA_DIRT', 'BEAMLASER': 'BEAMOS_LASER', 'MOLDTAIL': 'MOLDORM_TAIL',
                    'BARI': 'BARI_BLUE', 'HINOX': 'HINOX', 'LANMOLA': 'LANMOLA', 'LEEVER': 'LEEVER',
                    'BEAMOS': 'BEAMOS', 'POKEY': 'POKEY', 'GIBDO': 'GIBDO', 'MOLDORM': 'MOLDORM',
                    'HARDHAT': 'HARDHAT', 'BUMPER': 'BUMPER', 'BOMB': 'BOMB'}

# ---------------------------------------------------------------------------
# the enemies

PALETTES = []      # (name, 16 RGB colors)
SPRITES = {SPRITE_INDEX: [], SPRITE_INDEX_2: []}   # sprite slot -> its frames
FRAMES = SPRITES[SPRITE_INDEX]


def palette(name, cols):
    PALETTES.append((name, cols))


def frame(name, pix):
    FRAMES.append(Frame(name, pix))


def use_sprite(n):
    global FRAMES
    FRAMES = SPRITES[n]


def dmd(table, nt=1):
    """zelda3 DrawMultipleData entries (x, y, char_flags, ext) as pieces. A piece whose
    name-table bit, XORed with the sprite's own (nt), picks name table 0 is drawn from
    the shared item sheet (bombs, shadows, sand), which this leaves out."""
    out = []
    for (x, y, cf, ext) in table:
        flags = cf >> 8
        if not ((flags ^ nt) & 1):
            continue
        out.append((x, y, cf & 0xff, flags & 0xc0, ext))
    return out


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

    build_frames_2()


def two_palettes(low, high):
    """One 16-color palette from the low halves of two ALttP palettes (the second
    is drawn with color indices + 8)."""
    return low[:8] + high[:8]


def build_frames_2():
    use_sprite(SPRITE_INDEX_2)

    # --- Hinox (Dark World): walking each way, rearing back to throw, and an added
    # squash as the throw lands its weight
    hinox = Sheets(19, overworld=True)
    org = (8, 12)
    for g in range(12):
        frame('HINOX_%d' % g, compose(hinox, dmd(HINOX_DMD[g]), org))
    frame('HINOX_SQUASH', scaled(compose(hinox, dmd(HINOX_DMD[0]), org), 1.12, 0.86))

    # --- Lanmolas (Desert Palace boss): head and tail for each of 16 headings, a body
    # segment, the sand mound it bursts from and the dirt it throws up
    lm = Sheets(11)
    org = (8, 8)
    for a in range(16):
        frame('LANMOLA_HEAD_%d' % a, compose(lm, [(0, 0, LANMOLA_HEAD[a], LANMOLA_FLAGS[a], 2)], org))
    for a in range(16):
        frame('LANMOLA_TAIL_%d' % a, compose(lm, [(0, 0, LANMOLA_TAIL[a], LANMOLA_FLAGS[a], 2)], org))
    frame('LANMOLA_BODY', compose(lm, [(0, 0, 0xc6, 0, 2)], org))
    for i, (c, f) in enumerate(LANMOLA_MOUND):
        frame('LMDIRT_MOUND_%d' % i, compose(lm, [(0, 0, c, f, 2)], org))
    for i, pieces in enumerate(LANMOLA_SPRAY):
        frame('LMDIRT_SPRAY_%d' % i, compose(lm, pieces, org))

    desert = Sheets(10)
    # --- Leever: rising from the sand, spinning, sinking back
    org = (8, 12)
    for g in range(13):
        frame('LEEVER_%d' % g, compose(desert, leever_pieces(g), org))

    # --- Beamos: the statue with its eye at 16 points of its sweep (in front of the
    # dome for the near half), drawn in its own red (colors + 8)
    org = (8, 12)
    for n in range(16):
        i = n * 2
        eye = (BEAMOS_EYE_X[i] - 3, BEAMOS_EYE_Y[i] - 18, BEAMOS_EYE_C[i], BEAMOS_EYE_F[i], 0, 8)
        body = [(0, -16, 0x48, 0, 2), (0, 0, 0x68, 0, 2)]
        frame('BEAMOS_%d' % n, compose(desert, [eye] + body if i < 16 else body + [eye], org))
    # its laser: a streak of four bolts for each of 8 headings
    for a in range(8):
        dx = [1, 0.7, 0, -0.7, -1, -0.7, 0, 0.7][a]
        dy = [0, 0.7, 1, 0.7, 0, -0.7, -1, -0.7][a]
        pieces = [(int(round(-dx * 5 * k)) - 4, int(round(-dy * 5 * k)) - 4, 0x5c, 0, 0) for k in range(4)]
        frame('BEAMLASER_%d' % a, compose(desert, pieces, (0, 0)))

    # --- Pokey: a stack of one to four balls, settling (squashed) when it lands, and
    # a ball knocked loose
    pokey = Sheets(38)
    org = (8, 12)
    for n in range(1, 5):
        for d in (8, 6, 4):
            pieces = []
            for i in range(n - 1, -1, -1):
                c = (0xa2 if i == 0 else 0xa0) - (0x20 if d < 7 else 0)
                pieces.append((0, -(n - 1 - i) * d, c, 0, 2))
            frame('POKEY_%d_%d' % (n, d), compose(pokey, pieces, org))
    frame('POKEY_BALL', compose(pokey, [(0, 0, 0xa0, 0, 2)], org))

    # --- Gibdo (the mummy): 12 poses
    gibdo = Sheets(19)
    for g in range(12):
        frame('GIBDO_%d' % g, compose(gibdo, dmd(GIBDO_DMD[g]), org))

    # --- Moldorm (Tower of Hera boss): the head with its eyes for 16 headings, the big
    # segments, the small one and the tail, each spinning as it crawls
    md = Sheets(12)
    org = (8, 8)
    for d in range(16):
        eyes = []
        for r6 in ((d - 1) & 15, (d + 1) & 15):
            eyes.append((MOLDORM_EYE_X[r6], MOLDORM_EYE_Y[r6], MOLDORM_EYE_C[r6], MOLDORM_EYE_F[r6], 2))
        frame('MOLDORM_HEAD_%d' % d, compose(md, eyes + dmd(MOLDORM_HEAD), org))
    for v in range(2):
        frame('MOLDORM_SEG_%d' % v, compose(md, dmd(MOLDORM_SEG[v]), org))
    for v in range(4):
        frame('MOLDORM_SMALL_%d' % v, compose(md, [(0, 0, 0x88, MOLDORM_SPIN[v], 2)], org))
    for v in range(4):
        frame('MOLDTAIL_%d' % v, compose(md, [(0, 0, 0x8c, MOLDORM_SPIN[v], 2)], org))

    # --- Bari: two pulses, electrified, the small ones it splits into, and an added
    # stretch for a jellyfish bob
    bari = Sheets(19)
    org = (8, 12)
    for g in range(2):
        frame('BARI_%d' % g, compose(bari, BARI_DMD[g], org))
    frame('BARI_SHOCK', compose(bari, [(0, 0, 0x26, 0, 2)], org))
    frame('BARI_SMALL_0', compose(bari, [(4, 8, 0x20, 0, 0)], org))
    frame('BARI_SMALL_1', compose(bari, [(4, 8, 0x30, 0, 0)], org))
    frame('BARI_STRETCH', scaled(compose(bari, BARI_DMD[0], org), 0.88, 1.15))

    hera = Sheets(25)
    # --- Hardhat Beetle: two steps, and a squash when something bumps it
    for g in range(2):
        frame('HARDHAT_%d' % g, compose(hera, dmd(HARDHAT_DMD[g], nt=0), org))
    frame('HARDHAT_SQUASH', scaled(compose(hera, dmd(HARDHAT_DMD[0], nt=0), org), 1.15, 0.85))

    # --- Bumper: at rest, pulsing, and an added overshoot when it bounces Link away
    org = (8, 8)
    for g in range(2):
        frame('BUMPER_%d' % g, compose(hera, dmd(BUMPER_DMD[g]), org))
    frame('BUMPER_BOING', scaled(compose(hera, dmd(BUMPER_DMD[0]), org), 1.12, 1.12, dy=2))

    # the Hinox's bomb, from the shared sheet, and a swell as its fuse runs out
    misc = MiscSheet()
    org = (8, 12)
    bomb = compose(misc, [(0, 0, 0x6e, 0, 2)], org)
    frame('BOMB_0', bomb)
    frame('BOMB_SWELL', scaled(bomb, 1.15, 1.15, dy=1))

    palette('HINOX', mc_palette(3, mode=1))
    palette('BOMB', mc_palette(2))
    palette('LANMOLA', mc_palette(6, palset=4))
    palette('LANMOLA_DIRT', mc_palette(0, palset=4))
    palette('LEEVER', mc_palette(1, palset=9))
    palette('BEAMOS', two_palettes(mc_palette(6, palset=9), mc_palette(5, palset=9)))
    palette('BEAMOS_LASER', mc_palette(1, palset=9))
    palette('POKEY', mc_palette(6, palset=24))
    palette('GIBDO', mc_palette(6, palset=13))
    palette('MOLDORM', mc_palette(5, palset=6))
    palette('MOLDORM_TAIL', mc_palette(6, palset=6))
    palette('BARI_RED', mc_palette(1, palset=13))
    palette('BARI_BLUE', mc_palette(2, palset=13))
    palette('HARDHAT', mc_palette(4, palset=6))
    palette('BUMPER', mc_palette(2, palset=6))


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


def write_sprite(index, frames, suffix):
    """One sprite slot's tiles, frames, one-frame animations and OAM layouts."""
    gfx = bytearray()
    frames_asm = []
    objs_asm = ['@ generated by blend/enemies/alttp_enemies.py from your own cartridge; do not commit',
                '@ OAM layouts of sprite %d (A Link to the Past enemies)' % index,
                '\t.align 2',
                'BlendObjLists_%d::' % index]
    objs_body = []
    max_tiles = 0
    for n, f in enumerate(frames):
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
        objs_asm.append('\t.4byte BlendObj_%d_%d - gFrameObjLists' % (index, n))
        objs_body.append('BlendObj_%d_%d: @ %s\n\t.byte %d\n%s' % (index, n, f.name, len(objs), '\n'.join(lines)))
    # frame and animation numbers are bytes
    assert len(frames) < 255, 'sprite %d has too many frames: %d' % (index, len(frames))
    objs_asm += objs_body

    with open(os.path.join(OUT, 'alttp_enemies%s.4bpp' % suffix), 'wb') as fh:
        fh.write(gfx)
    with open(os.path.join(OUT, 'objlists', '%d.s' % index), 'w') as fh:
        fh.write('\n'.join(objs_asm) + '\n')
    inc = ['\t.align 2',
           'gSprite_AlttpEnemies%s::' % suffix,
           '\t.incbin "custom/gen/alttp_enemies%s.4bpp"' % suffix,
           '\t.align 2',
           'gSpriteFrames_AlttpEnemies%s::' % suffix] + frames_asm
    inc += ['\t.align 2', 'gSpriteAnimations_AlttpEnemies%s::' % suffix]
    inc += ['\t.4byte AlttpEnemy%sAnim_%d' % (suffix, n) for n in range(len(frames))]
    for n in range(len(frames)):
        # one frame held, looping onto itself; the enemy code picks frames itself
        inc.append('AlttpEnemy%sAnim_%d:\n\tkeyframe frame_index=%d, duration=255, bitfield2=0x80\n\t.byte 1'
                   % (suffix, n, n))
    print('alttp_enemies: sprite %d: %d frames, %d tiles (largest frame %d tiles)' % (
        index, len(frames), len(gfx) // 32, max_tiles))
    return inc


def write(preview=None):
    os.makedirs(os.path.join(OUT, 'objlists'), exist_ok=True)
    inc = ['@ generated by blend/enemies/alttp_enemies.py from your own cartridge; do not commit']
    inc += write_sprite(SPRITE_INDEX, SPRITES[SPRITE_INDEX], '')
    inc += write_sprite(SPRITE_INDEX_2, SPRITES[SPRITE_INDEX_2], '2')
    inc += ['\t.align 2', 'gAlttpEnemyPalettes::']
    for name, cols in PALETTES:
        inc.append('\t.2byte ' + ', '.join('0x%04x' % gba_color(c) for c in cols) + ' @ ' + name)
    with open(os.path.join(OUT, 'alttp_enemies.inc'), 'w') as fh:
        fh.write('\n'.join(inc) + '\n')

    h = ['/* generated by blend/enemies/alttp_enemies.py; frame and palette numbers */',
         '#ifndef ALTTP_ENEMIES_GEN_H', '#define ALTTP_ENEMIES_GEN_H',
         '#define ALTTP_ENEMY_SPRITE %d' % SPRITE_INDEX,
         '#define ALTTP_ENEMY_SPRITE_2 %d' % SPRITE_INDEX_2]
    slots = {}
    for index, frames in SPRITES.items():
        h.append('/* sprite %d */' % index)
        for n, f in enumerate(frames):
            h.append('#define AF_%s %d' % (f.name, n))
        for f in frames:
            kind = f.name.split('_')[0]
            slots[kind] = max(slots.get(kind, 1), (f.ntiles + 15) // 16)
    for kind, n in sorted(slots.items()):
        h.append('#define AS_%s %d /* 16-tile gfx slots its largest frame needs */' % (kind, n))
    for n, (name, cols) in enumerate(PALETTES):
        h.append('#define AP_%s %d' % (name, n))
    h.append('#endif')
    with open(os.path.join(OUT, 'alttp_enemies.h'), 'w') as fh:
        fh.write('\n'.join(h) + '\n')
    print('alttp_enemies: %d palettes' % len(PALETTES))

    if preview:
        draw_preview(preview)


def draw_preview(path):
    """Every frame on a sheet, for checking by eye (stays out of git: it is ROM art)."""
    from PIL import Image
    pal_for = {'STALFOS': 'STALFOS', 'EYEGORE': 'EYEGORE_GREEN', 'POPO': 'POPO',
               'ANTIFAIRY': 'ANTIFAIRY', 'ARMOS': 'ARMOS', 'ROPE': 'ROPE', 'MOBLIN': 'MOBLIN',
               'SPEAR': 'MOBLIN'}
    pal_for.update(PREVIEW_PALETTES)
    pals = dict(PALETTES)
    frames = SPRITES[SPRITE_INDEX] + SPRITES[SPRITE_INDEX_2]
    cols = 12
    rows = (len(frames) + cols - 1) // cols
    im = Image.new('RGB', (cols * 48, rows * 56), (40, 48, 40))
    for n, f in enumerate(frames):
        pal = pals[pal_for.get(f.name, pal_for.get(f.name.split('_')[0]))]
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
