"""
Agahnim (and his energy balls) for alttp_enemies.py: sprite 336. Ganon, his trident,
fire bats and flames, and the torches of his room: sprite 337.

Called by alttp_enemies.build_frames() with that module's globals, like
alttp_enemies_late.py. The draw tables are zelda3's (Agahnim_Draw,
Sprite_7B_AgahnimBalls, SeekerEnergyBall_Draw). Agahnim is drawn from Ganon's Tower's
sprite set (dungeon set 24, which also holds the pose he falls in), in the castle
tower's colors (palette 5 of room 0x20's palette set) and in Ganon's Tower's for the
second fight and its two shadows.
"""

import os
import struct

G = None  # alttp_enemies' globals


def frame(name, pix):
    G['frame'](name, pix)


def palette(name, cols):
    G['palette'](name, cols)


def compose(sheets, pieces, org):
    return G['compose'](sheets, pieces, org)


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


def define(line):
    G['EXTRA_DEFINES'].append(line)


def table_define(name, values):
    define('#define %s { %s }' % (name, ', '.join(str(v) for v in values)))


# ---------------------------------------------------------------------------
# zelda3's Agahnim_Draw: 18 poses of four pieces (12-16 his warp shadow, 17 fallen)

_X0 = [-8, 8] * 26 + [-6, 6, -6, 6, -8, 8, -8, 8, -6, 6, -6, 6, 0, 8, 0, 8, -8, 8, -8, 8]
_Y0 = [-8, -8, 8, 8] * 13 + [-6, -6, 6, 6, -8, -8, 8, 8, -6, -6, 6, 6, 0, 0, 8, 8, 8, 8, 8, 8]
_C0 = [0x82, 0x82, 0xa2, 0xa2, 0x80, 0x80, 0xa0, 0xa0, 0x84, 0x84, 0xa4, 0xa4, 0x86, 0x86, 0xa6, 0xa6,
       0x88, 0x8a, 0xa8, 0xaa, 0x8c, 0x8e, 0xac, 0xae, 0xc4, 0xc2, 0xe4, 0xe6, 0xc0, 0xc2, 0xe0, 0xe2,
       0x8a, 0x88, 0xaa, 0xa8, 0x8e, 0x8c, 0xae, 0xac, 0xc2, 0xc4, 0xe6, 0xe4, 0xc2, 0xc0, 0xe2, 0xe0,
       0xec, 0xec, 0xec, 0xec, 0xec, 0xec, 0xec, 0xec, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee,
       0xdf, 0xdf, 0xdf, 0xdf, 0x40, 0x42, 0x40, 0x42]
_F0 = [0, 0x40] * 8 + [0] * 16 + [0x40] * 16 + [0, 0x40, 0x80, 0xc0] * 5 + [0] * 4

# the spell between his hands (sprite_head_dir - 1: six growing stages for each of
# the six ways he faces), two pieces each
_X1 = [-7, 15, -11, 11, -11, 11, -8, 8, -4, 4, 0, 0, -10, -1, -14, -5,
       -14, -5, -12, -7, -10, -7, -10, -10, 16, 8, 12, 4, 12, 4, 10, 6,
       9, 7, 8, 8, -6, -6, -10, -10, -10, -10, -10, -10, -10, -10, -10, -10,
       14, 14, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, -7, 15, -11, 11,
       -11, 11, -8, 8, -4, 4, 0, 0]
_Y1 = [-5, -5, -9, -9, -9, -9, -9, -9, -9, -9, -9, -9, -3, 9, -7, 5,
       -7, 5, -5, 3, -3, 3, -2, -2, -3, 9, -7, 5, -7, 5, -5, 3,
       -3, 3, -2, -2, -3, 9, -7, 5, -7, 5, -5, 3, -3, 3, -2, -2,
       -3, 9, -7, 5, -7, 5, -5, 3, -3, 3, -2, -2, -5, -5, -9, -9,
       -9, -9, -9, -9, -9, -9, -9, -9]
_C1 = [0xce, 0xcc, 0xc6, 0xc6, 0xc6, 0xc6] * 6
_B1 = [0, 2, 2, 2, 2, 2] * 6


def build_agahnim():
    G['use_sprite'](G['SPRITE_INDEX_9'])
    V = G['Vram'](24)
    org = (8, 22)   # his feet; he floats a little above them
    poses = []
    for g in range(18):
        ps = []
        for i in range(4):
            j = g * 4 + i
            big = 0 if 64 <= j < 68 else 2
            ps.append((_X0[j], _Y0[j], 0x100 | _C0[j], _F0[j], big, 0))
        if g == 17:
            # the fallen pose is two 16x16 pieces; zelda3 lists each twice
            ps = ps[2:]
        poses.append(compose(V, ps, org))
    for g in range(12):
        frame('AGAHNIM_%d' % g, poses[g])
    for g in range(12, 17):
        frame('AGAHNIM_WARP_%d' % (g - 12), poses[g])
    frame('AGAHNIM_DEAD', poses[17])
    # added: the robe thinning into his warp shadow as he vanishes (and back), a
    # flinch when a ball strikes him, and a stretch as he calls the lightning down
    front = poses[0]
    for k in range(1, 4):
        keep = {}
        f = 1 - 0.18 * k
        for (x, y), c in front.items():
            if ((x * 7 + y * 5 + (x ^ y)) % 9) < k * 2:
                continue
            keep[(x, int(round(y * f)) + int(6 * (1 - f)))] = c
        frame('AGAHNIM_FADE_%d' % k, scaled(keep, 1 + 0.12 * k, 1.0))
    frame('AGAHNIM_HURT', scaled(poses[1], 1.14, 0.86, dy=1))
    frame('AGAHNIM_CALL', scaled(poses[1], 0.92, 1.12))

    # the spell in his hands
    for g in range(36):
        ps = []
        for i in (1, 0):
            ps.append((_X1[g * 2 + i], _Y1[g * 2 + i], 0x100 | _C1[g], 0, _B1[g], 0))
        frame('AGGLOW_%d' % g, compose(V, ps, org))

    org = (8, 8)
    # the energy ball (big, then the trail shrinking behind it), and the seeker that
    # bursts into six sparks
    for g, (c, big) in enumerate(((0xc6, 2), (0xcc, 2), (0xce, 0))):
        frame('AGBALL_%d' % g, compose(V, [(0 if big else 4, 0 if big else 4, 0x100 | c, 0, big, 0)], org))
    seek = [[(4, -3), (11, 4), (4, 11), (-3, 4)], [(-1, -1), (9, -1), (-1, 9), (9, 9)]]
    for g, pts in enumerate(seek):
        frame('AGBALL_SEEK_%d' % g, compose(V, [(x, y, 0x1ce, 0, 0, 0) for x, y in pts], org))
    frame('AGBALL_SPARK', compose(V, [(4, 4, 0x1ce, 0, 0, 0)], org))

    # colors: the castle tower (room 0x20, palette set 12) and Ganon's Tower (room
    # 0x0d, set 27); his shadows use palettes 4 and 5 there, as in A Link to the Past
    palette('AGAHNIM', mcpal(5, 12))
    palette('AGAHNIM_DW', mcpal(3, 27))
    palette('AGAHNIM_SHADOW1', mcpal(4, 27))
    palette('AGAHNIM_SHADOW2', mcpal(5, 27))
    palette('AGBALL_A', mcpal(1, 12))
    palette('AGBALL_B', mcpal(2, 12))


# ---------------------------------------------------------------------------
# Ganon (zelda3's Ganon_Draw, Trident_Draw, FireBat_Draw, Garnish10_GanonBatFlame):
# 17 poses of twelve 16x16 pieces, the head (two of them) turned three ways

_GX = [18, -8, 8, -8, 8, -18, -18, 18, -8, 8, -8, 8, 18, -8, 8, -8,
       8, -18, -18, 18, -8, 8, -8, 8, 16, -8, 8, -8, 8, -18, -18, 16,
       -8, 8, -11, 11, 16, -8, 8, -8, 8, -18, -18, 16, -8, 8, -11, 11,
       16, -8, 8, -8, 8, -18, -18, 16, -8, 8, -11, 11, 18, -8, 8, -8,
       8, -18, -18, 18, -8, 8, -8, 8, 18, -8, 8, -8, 8, -18, -18, 18,
       -8, 8, -8, 8, 18, -8, 8, -8, 8, -18, -18, 18, -8, 8, -11, 11,
       -8, 8, -8, 8, -8, 8, -8, 8, -18, -18, 18, 18, -8, 8, -8, 8,
       -8, 8, -8, 8, -18, -18, 18, 18, -8, 8, -8, 8, -8, 8, -10, 10,
       -18, -18, 18, 18, -8, 8, -8, 8, -8, 8, -10, 10, -18, -18, 18, 18,
       -8, 8, -8, 8, -8, 8, -10, 10, -18, -18, 18, 18, -8, 8, -8, 8,
       -8, 8, -8, 8, -18, -18, 18, 18, -8, 8, -8, 8, -8, 8, -8, 8,
       -18, -18, 18, 18, -7, -8, 8, -8, 8, -9, 8, -14, -14, -8, 8, 8,
       -8, 8, -8, 8, -18, -18, 18, 18, -8, 8, -11, 11]
_GY = [-8, -16, -16, -13, -13, -9, -1, -16, 3, 3, 8, 8, -8, -16, -16, -13,
       -13, -9, -1, -16, 3, 3, 8, 8, 5, -10, -10, -13, -13, -7, 1, -3,
       3, 3, 8, 8, 5, -10, -10, -13, -13, -7, 1, -3, 3, 3, 8, 8,
       5, -10, -10, -13, -13, -7, 1, -3, 3, 3, 8, 8, -1, -16, -16, -13,
       -13, -9, -1, -9, 3, 3, 8, 8, -10, -16, -16, -13, -13, -18, -10, -18,
       3, 3, 8, 8, 1, -10, -10, -13, -13, -7, 1, -7, 3, 3, 8, 8,
       -12, -12, 4, 4, -18, -18, 10, 10, -16, -8, -4, 4, -12, -12, 4, 4,
       -18, -18, 10, 10, -16, -8, -4, 4, -12, -12, 4, 4, -12, -12, 10, 10,
       -4, 4, -4, 4, -12, -12, 4, 4, -12, -12, 10, 10, -4, 4, -4, 4,
       -12, -12, 4, 4, -12, -12, 10, 10, -4, 4, -4, 4, -12, -12, 4, 4,
       -18, -18, 10, 10, -4, 4, -4, 4, -12, -12, 4, 4, -18, -18, 10, 10,
       -16, -8, -16, -8, -7, -12, -12, 4, 4, 7, 13, -11, -4, -16, -16, -16,
       -10, -10, -13, -13, -7, -7, -7, -7, 3, 3, 8, 8]
_GC = [0x16, 0x00, 0x00, 0x02, 0x02, 0x08, 0x18, 0x06, 0x22, 0x22, 0x20, 0x20, 0x46, 0x00, 0x00, 0x02,
       0x02, 0x08, 0x18, 0x36, 0x22, 0x22, 0x20, 0x20, 0x1a, 0x00, 0x00, 0x04, 0x04, 0x38, 0x48, 0x0a,
       0x24, 0x24, 0x20, 0x20, 0x1a, 0x40, 0x42, 0x04, 0x04, 0x38, 0x48, 0x0a, 0x24, 0x24, 0x20, 0x20,
       0x1a, 0x42, 0x40, 0x04, 0x04, 0x38, 0x48, 0x0a, 0x24, 0x24, 0x20, 0x20, 0x18, 0x00, 0x00, 0x02,
       0x02, 0x08, 0x18, 0x08, 0x22, 0x22, 0x20, 0x20, 0x16, 0x6a, 0x6a, 0x0e, 0x0e, 0x06, 0x16, 0x06,
       0x22, 0x22, 0x20, 0x20, 0x48, 0x00, 0x00, 0x04, 0x04, 0x38, 0x48, 0x38, 0x24, 0x24, 0x20, 0x20,
       0x4e, 0x4e, 0x6e, 0x6e, 0x6c, 0x6c, 0xa2, 0xa2, 0x0c, 0x1c, 0x3c, 0x4c, 0x4e, 0x4e, 0x6e, 0x6e,
       0x6c, 0x6c, 0xa2, 0xa2, 0x3a, 0x4a, 0x3c, 0x4c, 0x84, 0x84, 0xa4, 0xa4, 0xa0, 0xa0, 0xa2, 0xa2,
       0x3c, 0x4c, 0x3c, 0x4c, 0x84, 0x84, 0xa4, 0xa4, 0x80, 0x82, 0xa2, 0xa2, 0x3c, 0x4c, 0x3c, 0x4c,
       0x84, 0x84, 0xa4, 0xa4, 0x82, 0x80, 0xa2, 0xa2, 0x3c, 0x4c, 0x3c, 0x4c, 0x4e, 0x4e, 0x6e, 0x6e,
       0x6c, 0x6c, 0xa2, 0xa2, 0x3c, 0x4c, 0x3c, 0x4c, 0x4e, 0x4e, 0x6e, 0x6e, 0x6c, 0x6c, 0xa2, 0xa2,
       0x0c, 0x1c, 0x0c, 0x1c, 0xe0, 0xc6, 0xc8, 0xe6, 0xe8, 0x20, 0x20, 0x08, 0x18, 0xc0, 0xc2, 0xc2,
       0x00, 0x00, 0xce, 0xce, 0xec, 0xec, 0xec, 0xec, 0xee, 0xee, 0xc4, 0xc4]
_GF = [0x4c, 0x0c, 0x4c, 0x0a, 0x4a, 0x0c, 0x0c, 0x4c, 0x0a, 0x4a, 0x0c, 0x4c, 0x4c, 0x0c, 0x4c, 0x0a,
       0x4a, 0x0c, 0x0c, 0x4c, 0x0a, 0x4a, 0x0c, 0x4c, 0x4c, 0x0c, 0x4c, 0x0a, 0x4a, 0x0c, 0x0c, 0x4c,
       0x0a, 0x4a, 0x0c, 0x4c, 0x4c, 0x0c, 0x0c, 0x0a, 0x4a, 0x0c, 0x0c, 0x4c, 0x0a, 0x4a, 0x0c, 0x4c,
       0x4c, 0x4c, 0x4c, 0x0a, 0x4a, 0x0c, 0x0c, 0x4c, 0x0a, 0x4a, 0x0c, 0x4c, 0x4c, 0x0c, 0x4c, 0x0a,
       0x4a, 0x0c, 0x0c, 0x4c, 0x0a, 0x4a, 0x0c, 0x4c, 0x4c, 0x0c, 0x4c, 0x0a, 0x4a, 0x0c, 0x0c, 0x4c,
       0x0a, 0x4a, 0x0c, 0x4c, 0x4c, 0x0c, 0x4c, 0x0a, 0x4a, 0x0c, 0x0c, 0x4c, 0x0a, 0x4a, 0x0c, 0x4c,
       0x0a, 0x4a, 0x0a, 0x4a, 0x0c, 0x4c, 0x0c, 0x4c, 0x0c, 0x0c, 0x4c, 0x4c, 0x0a, 0x4a, 0x0a, 0x4a,
       0x0c, 0x4c, 0x0c, 0x4c, 0x0c, 0x0c, 0x4c, 0x4c, 0x0a, 0x4a, 0x0a, 0x4a, 0x0c, 0x4c, 0x0c, 0x4c,
       0x0c, 0x0c, 0x4c, 0x4c, 0x0a, 0x4a, 0x0a, 0x4a, 0x0c, 0x0c, 0x0c, 0x4c, 0x0c, 0x0c, 0x4c, 0x4c,
       0x0a, 0x4a, 0x0a, 0x4a, 0x4c, 0x4c, 0x0c, 0x4c, 0x0c, 0x0c, 0x4c, 0x4c, 0x0a, 0x4a, 0x0a, 0x4a,
       0x0c, 0x4c, 0x0c, 0x4c, 0x0c, 0x0c, 0x4c, 0x4c, 0x0a, 0x4a, 0x0a, 0x4a, 0x0c, 0x4c, 0x0c, 0x4c,
       0x0c, 0x0c, 0x4c, 0x4c, 0x0c, 0x0a, 0x0a, 0x0a, 0x0a, 0x0c, 0x4c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
       0x0c, 0x4c, 0x0a, 0x4a, 0x0c, 0x0c, 0x4c, 0x4c, 0x0a, 0x4a, 0x0c, 0x4c]
_TRIDENT = [(10, -10, 0x0864, 0), (5, -15, 0x0864, 0), (0, -20, 0x0864, 0), (-5, -25, 0x0864, 0), (-18, -38, 0x0844, 2),
            (1, -4, 0x0865, 0), (1, -11, 0x0865, 0), (1, -18, 0x0865, 0), (1, -25, 0x0865, 0), (-3, -40, 0x0862, 2),
            (-8, -9, 0x4864, 0), (-3, -14, 0x4864, 0), (3, -20, 0x4864, 0), (9, -26, 0x4864, 0), (12, -37, 0x4844, 2),
            (-10, -20, 0x4874, 0), (-3, -20, 0x4874, 0), (4, -20, 0x4874, 0), (11, -20, 0x4874, 0), (18, -23, 0x4860, 2),
            (-10, -30, 0xc864, 0), (-4, -24, 0xc864, 0), (2, -18, 0xc864, 0), (8, -12, 0xc864, 0), (12, -8, 0xc844, 2),
            (1, -32, 0x8865, 0), (1, -25, 0x8865, 0), (1, -18, 0x8865, 0), (1, -11, 0x8865, 0), (-3, -5, 0x8862, 2),
            (13, -30, 0x8864, 0), (8, -25, 0x8864, 0), (2, -19, 0x8864, 0), (-4, -13, 0x8864, 0), (-16, -9, 0x8844, 2),
            (14, -20, 0x0874, 0), (7, -20, 0x0874, 0), (0, -20, 0x0874, 0), (-7, -20, 0x0874, 0), (-21, -23, 0x0860, 2),
            (13, -30, 0x8864, 0), (8, -25, 0x8864, 0), (2, -19, 0x8864, 0), (-4, -13, 0x8864, 0), (-16, -9, 0x8844, 2),
            (-10, -30, 0xc864, 0), (-4, -24, 0xc864, 0), (-4, -24, 0xc864, 0), (-4, -24, 0xc864, 0), (-4, -24, 0xc864, 0)]

_HEAD_AT = [1, 1, 1, 1, 1, 1, None, 1, 4, 4, 4, 4, 4, 4, 4, None, None]
_HEAD_C = [0x40, 0x42, 0, 0, 0x42, 0x40, 0x82, 0x80, 0xa0, 0xa0, 0x80, 0x82]
_HEAD_F = [0, 0, 0, 0x40, 0x40, 0x40, 0x40, 0x40, 0, 0x40, 0, 0]
# the held trident's place by its pose (kTrident_Draw_X/Y: to his right, left, thrown,
# held upright on the right, on the left)
TRIDENT_AT_X = [24, -16, 0, 16, -8]
TRIDENT_AT_Y = [4, 4, 16, 21, 19]
_FB_C = [0x88, 0x88, 0x8a, 0x8c, 0x68, 0xaa, 0xa8]
_FB_F = [0, 0xc0, 0x80, 0x40, 0, 0x40, 0, 0x40, 0, 0x40, 0, 0x40, 0, 0x40]
# (9-bit chars: the first four in the room's set, the rest on the shared fire sheet)
_FLAME_C = [0x1ac, 0x1ac, 0x166, 0x166, 0x08e, 0x0a0, 0x0a2]
_FLAME_F = [1, 0x41, 1, 0x41, 0, 0, 0]
# where the torches stand in his room (0x00), 8-pixel cells of the two 16x16 torches,
# and the lit torch by the way in
GANON_TORCHES = [(37, 25), (57, 25)]
_TORCH_DARK = [0xde0, 0xdf0, 0x4de0, 0x4df0]


def ganon_pieces(g, h):
    """Pose g with the head turned h (0 one side, 1 ahead, 2 the other), its pieces
    in pal 5 (colors 1-7) and pal 6 (9-15) of the room's sprite palettes."""
    ps = []
    for i in range(12):
        j = g * 12 + i
        c, f = _GC[j], _GF[j]
        at = _HEAD_AT[g]
        if at is not None and i in (at, at + 1):
            k = h * 2 + (6 if g >= 8 else 0) + (i - at)
            c, f = _HEAD_C[k], (f & 0x3f) | _HEAD_F[k]
        add = 8 if (f >> 1) & 7 == 6 else 0
        ps.append((_GX[j], _GY[j], 0x100 | c, f & 0xc0, 2, add))
    return ps


def build_ganon():
    G['use_sprite'](G['SPRITE_INDEX_10'])
    V = G['Vram'](34)
    org = (0, 12)   # his feet
    names = []
    seen = {}
    for g in range(17):
        for h in range(3):
            if _HEAD_AT[g] is None and h:
                names.append(seen[g])
                continue
            pix = compose(V, ganon_pieces(g, h), org)
            name = 'GANON_%d_%d' % (g, h)
            frame(name, pix)
            seen[g] = name
            names.append(name)
    define('#define GANON_POSES { %s }' % ', '.join('AF_' + n for n in names))
    # his hand closed on the trident as he holds it upright (kGanon_Dmd, drawn while
    # the trident's pose is 9)
    for h in range(3):
        hand = [(16, -3, 0x10a, 0x40, 2, 8), (16, 5, 0x11a, 0x40, 2, 8)]
        frame('GANON_HOLD_%d' % h, compose(V, hand + ganon_pieces(2, h), org))
    # added: a crouch before he leaps, a stretch as he springs, and his fall
    frame('GANON_CROUCH', scaled(compose(V, ganon_pieces(6, 0), org), 1.12, 0.84))
    frame('GANON_SPRING', scaled(compose(V, ganon_pieces(6, 0), org), 0.9, 1.14))
    frame('GANON_FALL', scaled(compose(V, ganon_pieces(16, 0), org), 1.2, 0.7, dy=2))

    # the trident, about its middle: spinning (1-8), held upright (9, 10)
    org = (1, -20)
    for g in range(10):
        frame('TRIDENT_%d' % (g + 1), compose(V, G['pieces9'](_TRIDENT[g * 5:g * 5 + 5], own=1), org))

    # fire bats, and the flames they leave
    org = (0, 8)
    for g in range(7):
        ps = []
        for i in (1, 0):
            ps.append(((-8, 8)[i], 0, 0x100 | _FB_C[g], _FB_F[g * 2 + i], 2, 0))
        frame('FIREBAT_%d' % g, compose(V, ps, org))
    # the flame burning down: big, then flickering small (kGanonBatFlame_Idx)
    for k, g in enumerate((6, 5, 4, 0, 2)):
        frame('GFLAME_%d' % k, compose(V, [(0, 0, _FLAME_C[g], _FLAME_F[g] & 0xc0, 2, 0)], org))
    frame('GFLAME_WITHER', compose(V, [(0, 4, 0xa4, 0, 0, 0), (8, 4, 0xa5, 0, 0, 0)], org))

    # his room's torches (zelda3's lightable torch, tile data 0xec2), drawn from the
    # room's own background graphics; lit, a flame of the fire sheet burns on top (the
    # lit torch's own flame is an animated tile)
    import alttp_enemies_late as L
    L.boss_bg(0x00, 0x10)  # (the room's graphics, with the tileset of the way in below it)
    data = open(os.path.join(G['ROOT'], 'blend', 'out', 'z3', 'bossgfx', '000.gfx'), 'rb').read()
    chars, colors = data[:0x4000], struct.unpack('<256H', data[0x4000:0x4200])

    def tiles2x2(words, dx):
        pix = {}
        for n, w in enumerate(words):
            tx, ty = (n >> 1) * 8, (n & 1) * 8   # column by column (RoomDraw_Rightwards2x2)
            idx, pal = w & 0x3ff, (w >> 10) & 7
            b = chars[idx * 32:idx * 32 + 32]
            for y in range(8):
                p0, p1, p2, p3 = b[y * 2], b[y * 2 + 1], b[16 + y * 2], b[16 + y * 2 + 1]
                for x in range(8):
                    c = ((p0 >> (7 - x)) & 1) | (((p1 >> (7 - x)) & 1) << 1) | \
                        (((p2 >> (7 - x)) & 1) << 2) | (((p3 >> (7 - x)) & 1) << 3)
                    if c:
                        v = colors[pal * 16 + c]
                        px = 7 - x if w & 0x4000 else x
                        py = 7 - y if w & 0x8000 else y
                        pix[(tx + px + dx, ty + py)] = tuple((v >> s5 & 31) << 3 | (v >> s5 & 31) >> 2 for s5 in (0, 5, 10))
        return pix
    dark = L.indexed(tiles2x2(_TORCH_DARK, 0), 'GTORCH')
    y1 = max(p[1] for p in dark)
    frame('GTORCH', shifted(dark, 0, 7 - y1))

    # colors: his room's (palette set 33); stunned, the same colors turned blue
    pal5, pal6 = mcpal(5, 33), mcpal(6, 33)
    palette('GANON', G['two_palettes'](pal5, pal6))
    blue = [G['mc_palette'](p, tint=(72, 104, 240), palset=33) for p in (5, 6)]
    palette('GANON_STUN', G['two_palettes'](*blue))
    palette('TRIDENT', mcpal(4, 33))
    palette('FIREBAT', mcpal(1, 33))


def build(g):
    global G
    G = g
    build_agahnim()
    G['PREVIEW_PALETTES'].update({'AGAHNIM': 'AGAHNIM', 'AGGLOW': 'AGBALL_A', 'AGBALL': 'AGBALL_A'})
    build_ganon()
    G['PREVIEW_PALETTES'].update({'GANON': 'GANON', 'TRIDENT': 'TRIDENT', 'FIREBAT': 'FIREBAT', 'GFLAME': 'FIREBAT',
                                  'GTORCH': 'GTORCH'})
