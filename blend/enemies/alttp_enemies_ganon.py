"""
Agahnim (and his energy balls) for alttp_enemies.py: sprite 336.

Called by alttp_enemies.build_frames() with that module's globals, like
alttp_enemies_late.py. The draw tables are zelda3's (Agahnim_Draw,
Sprite_7B_AgahnimBalls, SeekerEnergyBall_Draw). Agahnim is drawn from Ganon's Tower's
sprite set (dungeon set 24, which also holds the pose he falls in), in the castle
tower's colors (palette 5 of room 0x20's palette set) and in Ganon's Tower's for the
second fight and its two shadows.
"""

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


def build(g):
    global G
    G = g
    build_agahnim()
    G['PREVIEW_PALETTES'].update({'AGAHNIM': 'AGAHNIM', 'AGGLOW': 'AGBALL_A', 'AGBALL': 'AGBALL_A'})
