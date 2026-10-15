"""
The later dungeons' enemies and bosses for alttp_enemies.py: Thieves' Town, the Ice
Palace, Misery Mire and Turtle Rock (sprites 333-335).

Called by alttp_enemies.build_frames() with that module's globals (its frame and
palette helpers). The draw layouts below are zelda3's (sprite_main.c); each enemy is
drawn from the sprite set and colors of a room it lives in. Kholdstare's ice shell
and Trinexx's rock body are background art in A Link to the Past (the room's first
layer), so they are cut from the boss room's own tilemap and made into sprites.
"""
import os
import struct
import subprocess

G = None  # alttp_enemies' globals


def frame(name, pix):
    G['frame'](name, pix)


def palette(name, cols):
    G['palette'](name, cols)


def compose(sheets, pieces, org):
    return G['compose'](sheets, pieces, org)


def pieces9(table, own=1, add_for=None):
    return G['pieces9'](table, own, add_for)


def mcpal(pal, palset):
    return G['mc_palette'](pal, palset=palset)


def scaled(pix, sx, sy, dy=0):
    return G['scaled'](pix, sx, sy, dy)


def mirrored(pix):
    return G['mirrored'](pix)


def shifted(pix, dx, dy):
    return G['shifted'](pix, dx, dy)


def merged(*layers):
    return G['merged'](*layers)


def dmd(entries):
    """zelda3 DrawMultipleData {x, y, char_flags, ext} lists as tuples."""
    return [tuple(e) for e in entries]


def single(char, flags=0, x=0, y=0, big=2):
    """One OAM piece in the sprite's own name table (SetOamHelper style)."""
    return (x, y, 0x100 | (char & 0xff), flags & 0xc0, big, 0)


def define(line):
    G['EXTRA_DEFINES'].append(line)


def table_define(name, values):
    define('#define %s { %s }' % (name, ', '.join(str(v) for v in values)))


# ---------------------------------------------------------------------------
# zelda3 draw tables

FREEZOR_DMD0 = [(-8, 0, 0x00a6, 2), (8, 0, 0x40a6, 2), (-8, 0, 0x00a6, 2), (8, 0, 0x40a6, 2),
                (-8, 0, 0x00a6, 2), (8, 0, 0x40a6, 2), (0, 11, 0x00ab, 0), (8, 11, 0x40ab, 0),
                (-8, 0, 0x00ac, 2), (8, 0, 0x40a8, 2), (0, 11, 0x00ba, 0), (8, 11, 0x00bb, 0),
                (-8, 0, 0x00a8, 2), (8, 0, 0x40ac, 2), (0, 11, 0x40bb, 0), (8, 11, 0x40ba, 0),
                (0, 2, 0x00ae, 0), (8, 2, 0x40ae, 0), (0, 10, 0x00be, 0), (8, 10, 0x40be, 0),
                (0, 4, 0x00af, 0), (8, 4, 0x40af, 0), (0, 12, 0x00bf, 0), (8, 12, 0x40bf, 0),
                (0, 8, 0x00aa, 0), (8, 8, 0x40aa, 0), (0, 8, 0x00aa, 0), (8, 8, 0x40aa, 0)]
FREEZOR_DMD1 = [(0, 0, 0x00ae, 0), (8, 0, 0x40ae, 0), (0, 8, 0x00be, 0), (8, 8, 0x40be, 0),
                (-2, 0, 0x00ae, 0), (10, 0, 0x40ae, 0), (-2, 8, 0x00be, 0), (10, 8, 0x40be, 0)]

PENGATOR_DMD0 = [(-1, -8, 0x0082, 2), (0, 0, 0x0088, 2), (-1, -7, 0x0082, 2), (0, 0, 0x008a, 2),
                 (-3, -6, 0x0082, 2), (0, 0, 0x0088, 2), (-6, -4, 0x0082, 2), (0, 0, 0x008a, 2),
                 (-4, 0, 0x00a2, 2), (4, 0, 0x00a3, 2), (1, -8, 0x4082, 2), (0, 0, 0x4088, 2),
                 (1, -7, 0x4082, 2), (0, 0, 0x408a, 2), (3, -6, 0x4082, 2), (0, 0, 0x4088, 2),
                 (6, -4, 0x4082, 2), (0, 0, 0x408a, 2), (4, 0, 0x40a2, 2), (-4, 0, 0x40a3, 2),
                 (0, -7, 0x0080, 2), (0, 0, 0x0086, 2), (0, -7, 0x4080, 2), (0, 0, 0x4086, 2),
                 (0, -4, 0x0080, 2), (0, 0, 0x0086, 2), (0, -1, 0x0080, 2), (0, 0, 0x0086, 2),
                 (-8, 0, 0x008e, 2), (8, 0, 0x408e, 2), (0, -8, 0x0084, 2), (0, 0, 0x008c, 2),
                 (0, -8, 0x4084, 2), (0, 0, 0x408c, 2), (0, -7, 0x0084, 2), (0, 0, 0x008c, 2),
                 (0, 0, 0x408c, 2), (0, -6, 0x4084, 2), (-8, 0, 0x00a0, 2), (8, 0, 0x40a0, 2)]
PENGATOR_DMD1 = [(0, 16, 0x00b5, 0), (8, 16, 0x40b5, 0), (0, -8, 0x00a5, 0), (8, -8, 0x40a5, 0)]

KNIGHT_DMD = [(-4, -8, 0x0064, 0), (-4, 0, 0x0061, 2), (4, 0, 0x0062, 2), (-3, 16, 0x0074, 0), (11, 16, 0x4074, 0),
              (-4, -7, 0x0064, 0), (-4, 1, 0x0061, 2), (4, 1, 0x0062, 2), (-3, 16, 0x0065, 0), (11, 16, 0x4065, 0),
              (-4, -8, 0x0048, 2), (4, -8, 0x0049, 2), (-4, 8, 0x004b, 2), (4, 8, 0x004c, 2), (4, 8, 0x004c, 2),
              (-4, 8, 0x0068, 2), (4, 8, 0x0069, 2), (4, 8, 0x0069, 2), (4, 8, 0x0069, 2), (4, 8, 0x0069, 2),
              (12, -7, 0x4064, 0), (-4, 1, 0x4062, 2), (4, 1, 0x4061, 2), (-3, 16, 0x0065, 0), (11, 16, 0x4065, 0)]
KNIGHT_HEAD = [(0x66, 0x40), (0x66, 0), (0x46, 0)]

WIZZROBE_DMD = [(0, -8, 0x00b2, 0), (8, -8, 0x00b3, 0), (0, 0, 0x0088, 2),
                (0, -8, 0x00b2, 0), (8, -8, 0x00b3, 0), (0, 0, 0x0086, 2),
                (0, -8, 0x00b2, 0), (8, -8, 0x00b3, 0), (0, 0, 0x008c, 2),
                (0, -8, 0x00b2, 0), (8, -8, 0x00b3, 0), (0, 0, 0x008a, 2),
                (0, -8, 0x00b2, 0), (8, -8, 0x00b3, 0), (0, 0, 0x408c, 2),
                (0, -8, 0x00b2, 0), (8, -8, 0x00b3, 0), (0, 0, 0x408a, 2),
                (0, -8, 0x00b2, 0), (8, -8, 0x00b3, 0), (0, 0, 0x00a4, 2),
                (0, -8, 0x00b2, 0), (8, -8, 0x00b3, 0), (0, 0, 0x008e, 2)]
WIZZBEAM_DMD = [(0, -4, 0x00c5, 0), (0, 4, 0x80c5, 0), (0, -4, 0x40c5, 0), (0, 4, 0xc0c5, 0),
                (-4, 0, 0x40d2, 0), (4, 0, 0x00d2, 0), (-4, 0, 0xc0d2, 0), (4, 0, 0x80d2, 0)]

LASEREYE_DMD = [(8, -4, 0x40c8, 0), (8, 4, 0x40d8, 0), (8, 12, 0xc0c8, 0),
                (8, -4, 0x40c9, 0), (8, 4, 0x40d9, 0), (8, 12, 0xc0c9, 0),
                (0, -4, 0x00c8, 0), (0, 4, 0x00d8, 0), (0, 12, 0x80c8, 0),
                (0, -4, 0x00c9, 0), (0, 4, 0x00d9, 0), (0, 12, 0x80c9, 0),
                (-4, 8, 0x00d6, 0), (4, 8, 0x00d7, 0), (12, 8, 0x40d6, 0),
                (-4, 8, 0x00c6, 0), (4, 8, 0x00c7, 0), (12, 8, 0x40c6, 0),
                (-4, 0, 0x80d6, 0), (4, 0, 0x80d7, 0), (12, 0, 0xc0d6, 0),
                (-4, 0, 0x80c6, 0), (4, 0, 0x80c7, 0), (12, 0, 0xc0c6, 0)]

ZAZAK_DMD = [(0, -8, 0x0008, 2), (-4, 0, 0x00a0, 2), (4, 0, 0x00a1, 2),
             (0, -7, 0x0008, 2), (-4, 1, 0x40a1, 2), (4, 1, 0x40a0, 2),
             (0, -8, 0x000e, 2), (-4, 0, 0x00a3, 2), (4, 0, 0x00a4, 2),
             (0, -7, 0x000e, 2), (-4, 1, 0x40a4, 2), (4, 1, 0x40a3, 2),
             (0, -9, 0x000c, 2), (0, 0, 0x00a6, 2), (0, 0, 0x00a6, 2),
             (0, -8, 0x000c, 2), (0, 0, 0x00a8, 2), (0, 0, 0x00a8, 2),
             (0, -9, 0x400c, 2), (0, 0, 0x40a6, 2), (0, 0, 0x40a6, 2),
             (0, -8, 0x400c, 2), (0, 0, 0x40a8, 2), (0, 0, 0x40a8, 2)]
ZAZAK_HEAD_CHAR = [0x82, 0x82, 0x80, 0x84, 0x88, 0x88, 0x86, 0x84]
ZAZAK_HEAD_FLAGS = [0x40, 0, 0, 0, 0x40, 0, 0, 0]
PHLEGM_DMD = [(0, 0, 0x00c3, 0), (-8, 0, 0x00c2, 0), (0, 0, 0x80c3, 0), (-8, 0, 0x80c2, 0),
              (0, 0, 0x40c3, 0), (8, 0, 0x40c2, 0), (0, 0, 0xc0c3, 0), (8, 0, 0xc0c2, 0),
              (0, 0, 0x00d4, 0), (0, -8, 0x00d3, 0), (0, 0, 0x40d4, 0), (0, -8, 0x40d3, 0),
              (0, 0, 0x80d4, 0), (0, 8, 0x80d3, 0), (0, 0, 0xc0d4, 0), (0, 8, 0xc0d3, 0)]

GIBO_DMD = [(4, -4, 0x408a, 2), (-4, -4, 0x408f, 0), (12, 12, 0x408e, 0), (-4, 4, 0x408c, 2),
            (4, -4, 0x40aa, 2), (-4, -4, 0x409f, 0), (12, 12, 0x409e, 0), (-4, 4, 0x40ac, 2),
            (3, -3, 0x40aa, 2), (-3, -3, 0x409f, 0), (11, 11, 0x409e, 0), (-3, 3, 0x40ac, 2),
            (3, -3, 0x408a, 2), (-3, -3, 0x408f, 0), (11, 11, 0x408e, 0), (-3, 3, 0x408c, 2),
            (-3, -4, 0x008a, 2), (13, -4, 0x008f, 0), (-3, 12, 0x008e, 0), (5, 4, 0x008c, 2),
            (-3, -4, 0x00aa, 2), (13, -4, 0x009f, 0), (-3, 12, 0x009e, 0), (5, 4, 0x00ac, 2),
            (-2, -3, 0x00aa, 2), (12, -3, 0x009f, 0), (-2, 11, 0x009e, 0), (4, 3, 0x00ac, 2),
            (-2, -3, 0x008a, 2), (12, -3, 0x008f, 0), (-2, 11, 0x008e, 0), (4, 3, 0x008c, 2)]

BLIND_DMD = [
    (-8, 7, 0x0c8e, 2), (8, 7, 0x4c8e, 2), (-8, 23, 0x0ca0, 2), (8, 23, 0x4ca4, 2), (0, 0, 0x0a8c, 2), (-19, 3, 0x0aa6, 2), (19, 3, 0x4aa6, 2),
    (-8, 7, 0x0c8e, 2), (8, 7, 0x4c8e, 2), (-8, 23, 0x0ca2, 2), (8, 23, 0x4ca0, 2), (0, 0, 0x0a8c, 2), (-19, 3, 0x0aa8, 2), (19, 3, 0x4aa8, 2),
    (-8, 7, 0x0c8e, 2), (8, 7, 0x4c8e, 2), (-8, 23, 0x0ca4, 2), (8, 23, 0x4ca2, 2), (0, 0, 0x0a8c, 2), (-19, 3, 0x0aaa, 2), (19, 3, 0x4aaa, 2),
    (-15, 5, 0x0aa6, 2), (-6, 7, 0x0c8e, 2), (6, 7, 0x4c8e, 2), (-6, 23, 0x0ca4, 2), (6, 23, 0x4ca0, 2), (0, 0, 0x0a8a, 2), (16, -1, 0x4aa6, 2),
    (-11, 9, 0x0aa6, 2), (-4, 7, 0x0c8e, 2), (5, 7, 0x4c8e, 2), (-4, 23, 0x0ca4, 2), (5, 23, 0x4ca0, 2), (0, 0, 0x0a88, 2), (10, -2, 0x4aa6, 2),
    (0, 0, 0x0a84, 2), (13, 8, 0x4aa6, 2), (-10, -2, 0x0aa6, 2), (-5, 7, 0x0c8e, 2), (5, 7, 0x4c8e, 2), (-5, 23, 0x0ca0, 2), (5, 23, 0x4ca4, 2),
    (0, 0, 0x0a82, 2), (18, 4, 0x4aa6, 2), (-15, -1, 0x0aa6, 2), (-6, 7, 0x0c8e, 2), (6, 7, 0x4c8e, 2), (-6, 23, 0x0ca0, 2), (6, 23, 0x4ca4, 2),
    (0, 0, 0x0a80, 2), (-19, 3, 0x0aa6, 2), (19, 3, 0x4aa6, 2), (-8, 7, 0x0c8e, 2), (8, 7, 0x4c8e, 2), (-8, 23, 0x0ca0, 2), (8, 23, 0x4ca4, 2),
    (0, 0, 0x0a80, 2), (-19, 3, 0x0aa8, 2), (19, 3, 0x4aa8, 2), (-8, 7, 0x0c8e, 2), (8, 7, 0x4c8e, 2), (-8, 23, 0x0ca2, 2), (8, 23, 0x4ca0, 2),
    (0, 0, 0x0a80, 2), (-19, 3, 0x0aaa, 2), (19, 3, 0x4aaa, 2), (-8, 7, 0x0c8e, 2), (8, 7, 0x4c8e, 2), (-8, 23, 0x0ca0, 2), (8, 23, 0x4ca4, 2),
    (-8, 9, 0x0c8e, 2), (8, 9, 0x4c8e, 2), (-8, 23, 0x0cae, 2), (8, 23, 0x4cae, 2), (8, 23, 0x4cae, 2), (8, 23, 0x4cae, 2), (0, 2, 0x0a8c, 2),
    (-8, 16, 0x0c8e, 2), (8, 16, 0x4c8e, 2), (-8, 23, 0x0cae, 2), (8, 23, 0x4cae, 2), (8, 23, 0x4cae, 2), (8, 23, 0x4cae, 2), (0, 9, 0x0a8c, 2),
    (-8, 23, 0x0cae, 2), (8, 23, 0x4cae, 2), (8, 23, 0x4cae, 2), (8, 23, 0x4cae, 2), (8, 23, 0x4cae, 2), (8, 23, 0x4cae, 2), (0, 16, 0x0a8c, 2),
    (-8, 23, 0x0cac, 2), (8, 23, 0x4cac, 2), (8, 23, 0x4cac, 2), (8, 23, 0x4cac, 2), (8, 23, 0x4cac, 2), (8, 23, 0x4cac, 2), (0, 20, 0x0a8c, 2),
    (-8, 23, 0x0cac, 2), (8, 23, 0x4cac, 2), (8, 23, 0x4cac, 2), (8, 23, 0x4cac, 2), (8, 23, 0x4cac, 2), (8, 23, 0x4cac, 2), (0, 23, 0x0a8c, 2),
]
BLIND_OAM_IDX = [4, 4, 4, 5, 5, 0, 0, 0, 0, 0]
BLIND_HEAD_CHAR = [0x86, 0x86, 0x84, 0x82, 0x80, 0x82, 0x84, 0x86, 0x86, 0x86, 0x88, 0x8a, 0x8c, 0x8a, 0x88, 0x86]
BLIND_HEAD_FLAGS = [0, 0, 0, 0, 0, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0, 0, 0, 0]
BLIND_LASER_GFX = [7, 7, 8, 9, 10, 9, 8, 7, 7, 7, 8, 9, 10, 9, 8, 7]
BLIND_LASER_FLAGS = [0, 0, 0, 0, 0, 0x40, 0x40, 0x40, 0x40, 0x40, 0xc0, 0xc0, 0x80, 0x80, 0x80, 0x80]
BLIND_LASER_CHARS = {7: 0x82, 8: 0xea, 9: 0xec, 10: 0x84}

KHOLD_DMD = [(-8, -8, 0x0080, 2), (8, -8, 0x0082, 2), (-8, 8, 0x00a0, 2), (8, 8, 0x00a2, 2),
             (-7, -7, 0x0080, 2), (7, -7, 0x0082, 2), (-7, 7, 0x00a0, 2), (7, 7, 0x00a2, 2),
             (-7, -7, 0x0084, 2), (7, -7, 0x0086, 2), (-7, 7, 0x00a4, 2), (7, 7, 0x00a6, 2),
             (-8, -8, 0x0084, 2), (8, -8, 0x0086, 2), (-8, 8, 0x00a4, 2), (8, 8, 0x00a6, 2)]
KHOLD_PUPIL_X = [8, 7, 4, 2, 0, -2, -4, -7, -8, -7, -4, -2, 0, 2, 4, 7]
KHOLD_PUPIL_Y = [0, 2, 4, 7, 8, 7, 4, 2, 0, -2, -4, -7, -8, -7, -4, -2]
KHOLD_PUPIL_C = [0xac, 0xac, 0xaa, 0x8c, 0x8c, 0x8c, 0xaa, 0xac, 0xac, 0xaa, 0xaa, 0x8c, 0x8c, 0x8c, 0xaa, 0xac]
KHOLD_PUPIL_F = [0x40, 0x40, 0x40, 0, 0, 0, 0, 0, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xc0, 0xc0]

VITREOUS_DMD = [(-8, -8, 0x01c0, 2), (8, -8, 0x41c0, 2), (-8, 8, 0x01e0, 2), (8, 8, 0x41e0, 2),
                (-8, -8, 0x01c8, 2), (8, -8, 0x01ca, 2), (-8, 8, 0x01e8, 2), (8, 8, 0x01ea, 2),
                (-8, -8, 0x41ca, 2), (8, -8, 0x41c8, 2), (-8, 8, 0x41ea, 2), (8, 8, 0x41e8, 2),
                (-8, -8, 0x01c2, 2), (8, -8, 0x41c2, 2), (-8, 8, 0x01e2, 2), (8, 8, 0x41e2, 2),
                (-8, -8, 0x01c4, 2), (8, -8, 0x41c4, 2), (-8, 8, 0x01e4, 2), (8, 8, 0x41e4, 2),
                (-7, -7, 0x01c4, 2), (7, -7, 0x41c4, 2), (-7, 7, 0x01e4, 2), (7, 7, 0x41e4, 2)]

TRINEXX_DMD = [(-8, -8, 0x40c0, 2), (8, -8, 0x00c0, 2), (-8, 8, 0x40e0, 2), (8, 8, 0x00e0, 2),
               (-8, -8, 0x0000, 2), (8, -8, 0x0002, 2), (-8, 8, 0x0020, 2), (8, 8, 0x0022, 2),
               (-8, -8, 0x00c2, 2), (8, -8, 0x00c4, 2), (-8, 8, 0x80c2, 2), (8, 8, 0x80c4, 2),
               (-8, -8, 0x8020, 2), (8, -8, 0x8022, 2), (-8, 8, 0x8000, 2), (8, 8, 0x8002, 2),
               (-8, -8, 0xc0e0, 2), (8, -8, 0x80e0, 2), (-8, 8, 0xc0c0, 2), (8, 8, 0x80c0, 2),
               (-8, -8, 0xc022, 2), (8, -8, 0xc020, 2), (-8, 8, 0xc002, 2), (8, 8, 0xc000, 2),
               (-8, -8, 0x40c4, 2), (8, -8, 0x40c2, 2), (-8, 8, 0xc0c4, 2), (8, 8, 0xc0c2, 2),
               (-8, -8, 0x4002, 2), (8, -8, 0x4000, 2), (-8, 8, 0x4022, 2), (8, 8, 0x4020, 2),
               (-8, -8, 0x0026, 2), (8, -8, 0x4026, 2), (-8, 8, 0x8026, 2), (8, 8, 0xc026, 2)]


def groups(table, n):
    return [table[i:i + n] for i in range(0, len(table), n)]


# ---------------------------------------------------------------------------
# background art cut from a boss room (Kholdstare's shell, Trinexx's body)

def boss_bg(room, entrance):
    """The room's first layer as pixels (x, y) -> RGB, and the colors it uses."""
    z3 = os.path.join(G['ROOT'], 'blend', 'out', 'z3')
    outdir = os.path.join(z3, 'bossgfx')
    path = os.path.join(outdir, '%03x.gfx' % room)
    if not os.path.exists(path):
        os.makedirs(outdir, exist_ok=True)
        subprocess.run(['./dumprooms', 'gfx', 'bossgfx', '%x' % entrance, '%x' % room], cwd=z3, check=True)
    data = open(path, 'rb').read()
    chars, colors = data[:0x4000], struct.unpack('<256H', data[0x4000:0x4200])
    raw = open(os.path.join(z3, 'rooms', '%03x.bin' % room), 'rb').read()
    bg1 = struct.unpack('<4096H', raw[0x4000:0x6000])
    pix = {}
    for i, w in enumerate(bg1):
        idx, pal = w & 0x3ff, (w >> 10) & 7
        if idx >= 512:
            continue
        b = chars[idx * 32:idx * 32 + 32]
        for y in range(8):
            p0, p1, p2, p3 = b[y * 2], b[y * 2 + 1], b[16 + y * 2], b[16 + y * 2 + 1]
            for x in range(8):
                c = ((p0 >> (7 - x)) & 1) | (((p1 >> (7 - x)) & 1) << 1) | \
                    (((p2 >> (7 - x)) & 1) << 2) | (((p3 >> (7 - x)) & 1) << 3)
                if not c:
                    continue
                dx = 7 - x if w & 0x4000 else x
                dy = 7 - y if w & 0x8000 else y
                v = colors[pal * 16 + c]
                rgb = ((v & 31) << 3 | (v & 31) >> 2, (v >> 5 & 31) << 3 | (v >> 5 & 31) >> 2,
                       (v >> 10 & 31) << 3 | (v >> 10 & 31) >> 2)
                pix[((i % 64) * 8 + dx, (i // 64) * 8 + dy)] = rgb
    return pix


def indexed(rgbpix, name, tint=None):
    """RGB pixels as palette indices (one palette, lit the blend's way), centred."""
    import blendstyle
    cols = sorted(set(rgbpix.values()), key=lambda c: sum(c))
    assert len(cols) <= 15, (name, len(cols))
    xs = [p[0] for p in rgbpix]
    ys = [p[1] for p in rgbpix]
    cx, cy = (min(xs) + max(xs) + 1) // 2, (min(ys) + max(ys) + 1) // 2
    out = {(x - cx, y - cy): cols.index(c) + 1 for (x, y), c in rgbpix.items()}
    pal = [(0, 0, 0)] + [blendstyle.blend_color(c) for c in cols]
    if tint:
        pal = [pal[0]] + [blendstyle.recolor_toward(c, tint) for c in pal[1:]]
    palette(name, pal + [(0, 0, 0)] * (16 - len(pal)))
    return out


def halves(pix):
    left = {p: c for p, c in pix.items() if p[0] < 0}
    right = {(x, y): c for (x, y), c in pix.items() if x >= 0}
    return left, right


def melted(pix, k, n):
    """The shell slumping as it melts: squashed down onto its base, with holes."""
    if k == 0:
        return pix
    y1 = max(y for x, y in pix)
    keep = {}
    f = 1 - k / (n + 0.5)
    for (x, y), c in pix.items():
        if ((x * 7 + y * 13) % 11) < k * 2:
            continue   # thinning, more each step
        ny = y1 - int((y1 - y) * f)
        keep[(x, ny)] = c
    return keep


def cracked(pix, k):
    """Damage on the shell: a scatter of the darkest color, thicker each step."""
    if k == 0:
        return pix
    out = dict(pix)
    for (x, y), c in pix.items():
        if (x * 5 + y * 3 + x * y) % 23 < k * 2 and (x + y) % 3 == 0:
            out[(x, y)] = 1
    return out


# ---------------------------------------------------------------------------

def build_enemies():
    """Sprite 333: the four dungeons' common enemies."""
    G['use_sprite'](G['SPRITE_INDEX_6'])
    Vram = G['Vram']
    ice = Vram(28)
    ice2 = Vram(41)
    mire = Vram(29)
    tr = Vram(37)
    tr2 = Vram(30)
    tt = Vram(27)

    # --- Freezor: the ice statue in the wall, waking, gliding and melting
    org = (0, 8)
    for g, ps in enumerate(groups(FREEZOR_DMD0, 4)):
        frame('FREEZOR_%d' % g, compose(ice, pieces9(ps, own=0x0d), org))
    frame('FREEZOR_7', compose(ice, pieces9(FREEZOR_DMD1, own=0x0d), org))
    # added: a lean as it glides, and a crack as it thaws
    frame('FREEZOR_LEAN', shifted(scaled(compose(ice, pieces9(groups(FREEZOR_DMD0, 4)[1], own=0x0d), org), 0.92, 1.06), 1, 0))
    palette('FREEZOR', mcpal(6, 19))

    # --- Pengator: 20 poses (walk, crouch, leap and belly slide, each way)
    org = (8, 12)
    for g, ps in enumerate(groups(PENGATOR_DMD0, 2)):
        extra = []
        if g == 14:
            extra = PENGATOR_DMD1[:2]
        elif g == 19:
            extra = PENGATOR_DMD1[2:]
        frame('PENGATOR_%d' % g, compose(ice, pieces9(ps + extra, own=0x0b), org))
    # the shared green of palette 5 made the Ice Palace's icy blue
    import blendstyle
    pen = mcpal(5, 19)
    for i in (6, 7, 14, 15):
        pen[i] = blendstyle.recolor_toward(pen[i], (60, 110, 230))
    palette('PENGATOR', pen)

    # --- Stalfos Knight: body poses with the head each way it looks, its heap of
    # bones (the head bobbing down onto it), and the shudder as it pulls together
    org = (4, 16)
    body = groups(KNIGHT_DMD, 5)
    head_c = {0: 0, 1: 1, 2: 0, 3: 0, 4: 1}
    for g in (0, 1, 3, 4):
        for h, (c, f) in enumerate(KNIGHT_HEAD):
            if g == 3 and h != 2:
                continue
            ps = [snes(0, head_c.get(g, 0) - 12, c, f, 2, 0x0b)] + pieces9(body[g], own=0x0b)
            frame('KNIGHT_%d_%d' % (g, h), compose(ice2, ps, org))
    frame('KNIGHT_2', compose(ice2, pieces9(body[2], own=0x0b), org))
    for c in (4, 8, 12, 14):
        ps = [snes(0, c - 12, 0x46, 0, 2, 0x0b)] + pieces9(body[3], own=0x0b)
        frame('KNIGHT_PILE_%d' % c, compose(ice2, ps, org))
    palette('KNIGHT', mcpal(5, 19))

    # --- Wizzrobe: eight poses (two each way), and its beam
    org = (0, 8)
    for g, ps in enumerate(groups(WIZZROBE_DMD, 3)):
        frame('WIZZROBE_%d' % g, compose(mire, pieces9(ps, own=0x01), org))
    for d in range(4):
        frame('WIZZBEAM_%d' % d, compose(mire, pieces9(WIZZBEAM_DMD[d * 2:d * 2 + 2], own=0x02), (4, 4)))
    palette('WIZZROBE', mcpal(0, 17))
    palette('WIZZBEAM', mcpal(1, 17))

    # --- Laser Eye: shut and open, on each of the four walls; its beam
    org = (4, 8)
    for d in range(4):
        for g in range(2):
            frame('LASEREYE_%d_%d' % (d, g), compose(tr, pieces9(LASEREYE_DMD[(g + d * 2) * 3:(g + d * 2) * 3 + 3], own=1), org))
    frame('LASER_0', compose(tr, [single(0xd2, 0, big=0)], (4, 4)))
    frame('LASER_1', compose(tr, [single(0xf3, 0, big=0)], (4, 4)))
    palette('LASEREYE', mcpal(0, 24))
    palette('LASER', mcpal(2, 24))

    # --- Chain Chomp: five poses round the compass (mirrored for the other side),
    # a chain link, and an added bite with its jaws wide
    org = (8, 8)
    for g, c in enumerate((0x8c, 0xaa, 0x86, 0xa8, 0xa6)):
        p = compose(tr2, [single(c)], org)
        frame('CHOMP_%d' % g, p)
        frame('CHOMP_%d_FLIP' % g, mirrored(p))
    frame('CHOMP_BITE', scaled(compose(tr2, [single(0x8c)], org), 1.15, 1.15, dy=1))
    frame('CHOMPLINK', compose(tr2, [single(0x8b, big=0)], (4, 4)))
    palette('CHOMP', mcpal(4, 24))
    palette('CHOMPLINK', mcpal(6, 24))

    # --- Zazak: the lizard men, walking each way with the head looking any way,
    # and the open mouth as one breathes fire; its fireball
    org = (8, 12)
    for g, ps in enumerate(groups(ZAZAK_DMD, 3)):
        for h in range(4):
            head = snes(ps[0][0], ps[0][1], ZAZAK_HEAD_CHAR[h], ZAZAK_HEAD_FLAGS[h], 2, 0x09)
            frame('ZAZAK_%d_%d' % (g, h), compose(tt, [head] + pieces9(ps[1:], own=0x09), org))
    for h in range(4):
        g = (4, 6, 0, 2)[h]  # standing pose facing that way
        ps = groups(ZAZAK_DMD, 3)[g]
        head = snes(ps[0][0], ps[0][1], ZAZAK_HEAD_CHAR[h + 4], ZAZAK_HEAD_FLAGS[h + 4], 2, 0x09)
        frame('ZAZAK_FIRE_%d' % h, compose(tt, [head] + pieces9(ps[1:], own=0x09), org))
    for d in range(4):
        for g in range(2):
            frame('PHLEGM_%d_%d' % (d, g), compose(tt, pieces9(PHLEGM_DMD[d * 4 + g * 2:d * 4 + g * 2 + 2], own=0x02), (4, 4)))
    palette('ZAZAK_BLUE', mcpal(4, 23))
    palette('ZAZAK_RED', mcpal(3, 23))
    palette('PHLEGM', mcpal(1, 23))

    # --- Gibo: the jelly shell's four wobbles each way, and the nucleus spinning
    org = (8, 8)
    for g in range(8):
        frame('GIBO_%d' % g, compose(tt, pieces9(GIBO_DMD[g * 4:g * 4 + 4], own=0x0b), org))
    core = compose(tt, [single(0xae)], org)
    frame('GIBOCORE_0', core)
    frame('GIBOCORE_1', mirrored(core))
    frame('GIBOCORE_2', {(-1 - x, -1 - y): c for (x, y), c in core.items()})
    frame('GIBOCORE_3', {(x, -1 - y): c for (x, y), c in core.items()})
    palette('GIBO', mcpal(5, 23))
    palette('GIBOCORE', mcpal(3, 23))

    # --- fire bar (Guruguru bar): its fireballs, spun a quarter turn at a time
    for g, f in enumerate((0, 0x40, 0xc0, 0x80)):
        frame('FIREBAR_%d' % g, compose(ice, [single(0x28, f)], org))
    palette('FIREBAR', mcpal(1, 19))

    # --- the Stalfos head (sprite 0x7C): a skull that creeps up while Link looks away
    sk = Vram(29)
    org = (8, 8)
    p = compose(sk, [single(0x02)], org)
    frame('SKULL_0', mirrored(p))
    frame('SKULL_1', p)
    frame('SKULL_2', compose(sk, [single(0x00)], org))
    frame('SKULL_3', compose(sk, [single(0x04)], org))
    palette('SKULL', mcpal(6, 17))

    # --- falling ice (Kholdstare's), shared with Trinexx's ice breath: a big
    # block, a chunk, and two shatter poses
    kh = Vram(22)
    for g, c in enumerate((0x8a, 0x8e, 0x8a, 0x86)):
        frame('ICEBALL_%d' % g, compose(kh, [single(c)], org))
    frame('ICEBALL_SPIN', mirrored(compose(kh, [single(0x8e)], org)))
    palette('ICEBALL', mcpal(6, 20))


def snes(x, y, char, flags, big, own):
    """A piece placed by SetOamHelper with the sprite's own OAM flags."""
    f = flags ^ own
    return (x, y, (char & 0xff) | (f & 1) << 8, f & 0xc0, big, 0)


def build_blind_kholdstare():
    """Sprite 334: Blind (Thieves' Town) and Kholdstare (Ice Palace)."""
    G['use_sprite'](G['SPRITE_INDEX_7'])
    Vram = G['Vram']
    bl = Vram(32)
    # --- Blind: the robed body in its fifteen poses (sway, turn, behind the
    # curtain), its head (a part of its own, since it turns on its own), the laser
    # from its eyes. Colors: head palette 5, robes palette 6 (colors + 8).
    org = (8, 20)
    hx, hy = [], []
    for g, ps in enumerate(groups(BLIND_DMD, 7)):
        ps = list(ps)
        if g < 10:
            h = ps.pop(BLIND_OAM_IDX[g])
            hx.append(h[0])
            hy.append(h[1] - 12)
        else:
            hx.append(0)
            hy.append(0)
        frame('BLIND_%d' % g, compose(bl, pieces9(ps, own=0x01, add_for={6: 8}), org))
    table_define('BLIND_HEAD_DX', hx)
    table_define('BLIND_HEAD_DY', hy)
    # the maiden Blind poses as. A Link to the Past draws her as its follower does
    # (Blind_Draw's pose 21: chars 0x20 and 0x22, her head and dress, which the
    # follower code fills from sprite sheet 0x65's first row: heads 0-2, dresses 3-6).
    # Her step and her turns are the follower's; added: a shudder and a stretch as
    # Blind tears out of her.
    ms = G['MiscSheet'](0x65)

    def maiden(head, body, hflip=0, bflip=0):
        return compose(ms, [(0, 13, head * 2, hflip, 2, 0), (0, 23, body * 2, bflip, 2, 0)], org)
    frame('MAIDEN_0', maiden(0, 5))
    frame('MAIDEN_1', maiden(0, 5, 0, 0x40))
    frame('MAIDEN_UP_0', maiden(1, 6))
    frame('MAIDEN_UP_1', maiden(1, 6, 0, 0x40))
    frame('MAIDEN_SIDE_0', maiden(2, 3))
    frame('MAIDEN_SIDE_1', maiden(2, 4))
    frame('MAIDEN_SIDE_2', maiden(2, 3, 0x40, 0x40))
    frame('MAIDEN_SIDE_3', maiden(2, 4, 0x40, 0x40))
    frame('MAIDEN_SHAKE', scaled(maiden(0, 5), 1.15, 0.88))
    frame('MAIDEN_RISE', scaled(maiden(0, 5), 0.85, 1.18))
    org = (8, 8)
    for c, f in sorted(set(zip(BLIND_HEAD_CHAR, BLIND_HEAD_FLAGS))):
        frame('BLINDHEAD_%02x_%d' % (c, f >> 6), compose(bl, [single(c, f)], org))
    table_define('BLIND_HEAD_FRAME', ['AF_BLINDHEAD_%02x_%d' % (c, f >> 6) for c, f in zip(BLIND_HEAD_CHAR, BLIND_HEAD_FLAGS)])
    # its laser: in A Link to the Past only a trail of sparks (a garnish) draws it;
    # here a bolt of light along its heading, a bright core in a glow (palette 1)
    import math
    for d in range(16):
        a = math.pi * d / 8   # 0 east, 4 south
        dx, dy = -math.cos(a), math.sin(a)
        pix = {}
        for t in range(-6, 7):
            for w in (-1, 0, 1):
                x = int(round(t * dx - w * dy))
                y = int(round(t * dy + w * dx))
                c = 1 if w == 0 and abs(t) < 5 else 4
                if pix.get((x, y), 99) > c:
                    pix[(x, y)] = c
        frame('BLINDLASER_%d' % d, pix)
    palette('BLIND', G['two_palettes'](mcpal(5, 23), mcpal(6, 23)))
    palette('BLINDHEAD', mcpal(5, 23))
    palette('BLINDLASER', mcpal(1, 23))

    # --- Kholdstare: the eye's four looks, its pupil following Link (a part of its
    # own), and the ice shell it hides in, in two halves, cracking as fire hits it
    # and slumping into a puddle as it melts
    kh = Vram(22)
    for g, ps in enumerate(groups(KHOLD_DMD, 4)):
        frame('KHOLD_%d' % g, compose(kh, pieces9(ps, own=0x0d), org))
    frame('KHOLD_SQUASH', scaled(compose(kh, pieces9(KHOLD_DMD[:4], own=0x0d), org), 1.15, 0.85, dy=2))
    for j in range(16):
        frame('KPUPIL_%d' % j, compose(kh, [single(KHOLD_PUPIL_C[j], KHOLD_PUPIL_F[j], KHOLD_PUPIL_X[j], KHOLD_PUPIL_Y[j])], org))
    palette('KHOLD', mcpal(6, 20))
    shell = indexed(boss_bg(0xde, 0x0e), 'KSHELL')
    for side, half in zip('LR', halves(shell)):
        for k in range(3):
            frame('KSHELL_%s_%d' % (side, k), cracked(half, k))
        for k in range(1, 4):
            frame('KSHELL_%s_MELT_%d' % (side, k), melted(half, k, 4))


def build_vitreous_trinexx():
    """Sprite 335: Vitreous (Misery Mire) and Trinexx (Turtle Rock)."""
    G['use_sprite'](G['SPRITE_INDEX_8'])
    Vram = G['Vram']
    vi = Vram(22)
    org = (8, 8)
    # --- Vitreous: the big eye's six looks; the small eyes, open and shut (and an
    # added blink); the lightning it spews, four forks each way
    for g, ps in enumerate(groups(VITREOUS_DMD, 4)):
        frame('VITREOUS_%d' % g, compose(vi, pieces9(ps, own=0x0a), org))
    frame('VITREOUS_HOP', scaled(compose(vi, pieces9(VITREOUS_DMD[:4], own=0x0a), org), 0.9, 1.12))
    frame('VITREOUS_LAND', scaled(compose(vi, pieces9(VITREOUS_DMD[:4], own=0x0a), org), 1.18, 0.82, dy=2))
    for g, c in enumerate((0xe6, 0xc6)):
        p = compose(vi, [single(c)], org)
        frame('VEYE_%d' % g, p)
    frame('VEYE_BLINK', scaled(compose(vi, [single(0xe6)], org), 1.1, 0.6, dy=-2))
    for g, c in enumerate((0xcc, 0xec, 0xce, 0xee)):
        p = compose(vi, [single(c)], org)
        frame('BOLT_%d' % g, p)
        frame('BOLT_%d_FLIP' % g, mirrored(p))
    palette('VITREOUS', mcpal(5, 18))
    palette('BOLT', mcpal(5, 18))

    # --- Trinexx: the rock head's nine looks; its stone body (cut from the room,
    # in two halves) with the legs stepping, four steps; the side heads with their
    # jaws opening, a neck segment; and the snake's body for the last phase
    tx = Vram(23)
    for g, ps in enumerate(groups(TRINEXX_DMD, 4)):
        frame('TRINEXX_%d' % g, compose(tx, pieces9(ps, own=0x01), org))
    for g, c in enumerate((0x2c, 0x28, 0x06)):
        frame('TSEG_%d' % g, compose(tx, [single(c)], org))
    body = indexed(boss_bg(0xa4, 0xd6), 'TSHELL')
    # legs (SpriteDraw_TrinexxRockHeadAndBody): 16x32 at the shell's sides, bobbing
    leg_x = [0, 2, 3, 4, 4, 4, 3, 2, 0, -2, -3, -4, -4, -4, -3, -2]
    leg_y = [-4, -4, -3, -2, 0, 2, 3, 4, 4, 4, 3, 2, 0, -2, -3, -4]
    left, right = halves(body)
    for step in range(4):
        j = step * 4
        legs = {}
        for i in (1, 0):
            x = (-28 if i else 28) + leg_x[(j + (1 - i) * 8) & 15]
            y = 12 + leg_y[(j + i * 8) & 15]
            f = 0 if i else 0x40
            p = compose(tx, [single(0x0c, f, x, y), single(0x2a, f, x, y + 16)], (8, 8))
            legs.update(p)
        # the legs in the shell's own colors (palette TSHELL) would not match; they
        # take the head's colors, so they go behind the shell as frames of their own
        frame('TLEGS_%d' % step, legs)
    for side, half in zip('LR', (left, right)):
        frame('TSHELL_%s' % side, half)
    for jaw in (0, 2, 4, 6, 8):
        ps = [single(c, f, x, y + (jaw if m == 4 else 0)) for m, (x, y, c, f) in
              enumerate(((-8, -8, 4, 0x40), (8, -8, 4, 0), (-8, 8, 0x24, 0x40), (8, 8, 0x24, 0), (0, 2, 0x0a, 0)))]
        frame('THEAD_%d' % jaw, compose(tx, ps, org))
    frame('TNECK', compose(tx, [single(0x08)], org))
    palette('TRINEXX', mcpal(0, 25))
    palette('TFIRE', mcpal(5, 25))
    palette('TICE', mcpal(6, 25))


def build(g):
    global G
    G = g
    build_enemies()
    build_blind_kholdstare()
    build_vitreous_trinexx()
    G['PREVIEW_PALETTES'].update({
        'FREEZOR': 'FREEZOR', 'PENGATOR': 'PENGATOR', 'KNIGHT': 'KNIGHT', 'WIZZROBE': 'WIZZROBE',
        'WIZZBEAM': 'WIZZBEAM', 'LASEREYE': 'LASEREYE', 'LASER': 'LASER', 'CHOMP': 'CHOMP',
        'CHOMPLINK': 'CHOMPLINK', 'ZAZAK': 'ZAZAK_RED', 'PHLEGM': 'PHLEGM', 'GIBO': 'GIBO',
        'GIBOCORE': 'GIBOCORE', 'FIREBAR': 'FIREBAR', 'SKULL': 'SKULL', 'ICEBALL': 'ICEBALL',
        'BLIND': 'BLIND', 'MAIDEN': 'BLIND', 'BLINDHEAD': 'BLINDHEAD', 'BLINDLASER': 'BLINDLASER', 'KHOLD': 'KHOLD',
        'KPUPIL': 'KHOLD', 'KSHELL': 'KSHELL', 'VITREOUS': 'VITREOUS', 'VEYE': 'VITREOUS',
        'BOLT': 'BOLT', 'TRINEXX': 'TRINEXX', 'TSEG': 'TRINEXX', 'TSHELL': 'TSHELL',
        'TLEGS': 'TRINEXX', 'THEAD': 'TFIRE', 'TNECK': 'TFIRE'})
