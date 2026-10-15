"""
Ganon's Tower's last A Link to the Past enemies for alttp_enemies.py: sprite 340.

The blue sword soldier and the red spear soldier, the wall cannons and their
cannonballs, the flying floor tiles, and the Sparks that run along the walls.

Called by alttp_enemies.build_frames() with that module's globals, like
alttp_enemies_late.py. The draw tables are zelda3's (Guard_AnimateHead / Body /
Weapon, Sprite_66_WallCannonVerticalLeft, Sprite_Cannonball, FlyingTile_Draw,
Sprite_5B_Spark_Clockwise). Each is drawn from the sprite set and colors of a Ganon's
Tower room it stands in: the soldiers from room 0xA5 (sprite set 36, palette set 37),
the cannons from room 0x5C (set 10, palettes 27), the tiles from room 0x8D (set 19,
palettes 40), the Sparks from room 0x3D (set 12, palettes 26).
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


def shifted(pix, dx, dy):
    return G['shifted'](pix, dx, dy)


def merged(*layers):
    return G['merged'](*layers)


# ---------------------------------------------------------------------------
# zelda3's soldier tables (sprite_main.c): poses 0-13 (0-3 walking down, 4-6 left,
# 7-10 up, 11-13 right); the head (one 16x16 char by the way it looks) sits Draw1_Yd
# above the body; four body pieces and two 8x8 weapon pieces each pose

_D1_YD = [7, 8, 7, 8, 8, 7, 8, 7, 8, 7, 8, 8, 7, 8]
_HEAD = [(0x42, 0x40), (0x42, 0), (0x40, 0), (0x44, 0)]   # right, left, down, up
_D2_X = [-4, 4, 10, 10, -4, 4, 10, 10, -4, 4, 10, 10, -4, 4, 10, 10, -4, -4, 0, 0, -4, -4, 0, 0, -3, -3, 0, 0,
         -3, -3, -4, 4, -3, -3, -4, 4, -3, -3, -4, 4, -3, -3, -4, 4, 12, 12, 0, 0, 12, 12, 0, 0, 11, 11, 0, 0]
_D2_Y = [0, 0, 2, 10, 0, 0, 2, 10, 0, 0, 1, 9, 0, 0, 2, 10, -2, 6, 1, 1, -2, 6, 2, 2, -2, 6, 1, 1,
         -5, 3, 0, 0, -4, 4, 0, 0, -4, 4, 0, 0, -5, 3, 0, 0, -2, 6, 1, 1, -2, 6, 2, 2, -2, 6, 1, 1]
_D2_C = [72, 73, 109, 125, 73, 72, 109, 125, 70, 70, 109, 125, 75, 70, 109, 125, 77, 93, 78, 78, 77, 93, 96, 96,
         77, 93, 98, 98, 109, 125, 100, 100, 109, 125, 102, 103, 109, 125, 103, 102, 109, 125, 100, 105,
         77, 93, 78, 78, 77, 93, 96, 96, 77, 93, 98, 98]
_D2_F = [0, 0, 0, 0, 64, 64, 0, 0, 0, 64, 0, 0, 0, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 64, 0, 0, 0, 0, 0, 0, 64, 64, 0, 0, 0, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64]
_D2_B = [2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2,
         0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2]
_D3_X = [-3, -3, -4, -4, -4, -4, -4, -4, -11, -3, -11, -3, -16, -8, 12, 12, 12, 12, 12, 12, 12, 12,
         21, 13, 21, 13, 24, 16]
_D3_Y = [11, 19, 11, 19, 10, 18, 14, 22, 8, 8, 8, 8, 6, 6, -10, -2, -9, -1, -9, -1, -16, -8, 8, 8, 8, 8, 6, 6]
_D3_C = [123, 107, 123, 107, 123, 107, 123, 107, 108, 124, 108, 124, 108, 124, 107, 123, 107, 123, 107, 123,
         107, 123, 108, 124, 108, 124, 108, 124]
_D3_F = [128] * 8 + [0] * 14 + [64] * 6
# which way each pose faces (0 right, 1 left, 2 down, 3 up)
POSE_FACE = [2, 2, 2, 2, 1, 1, 1, 3, 3, 3, 3, 0, 0, 0]
# the poses a soldier walks through each way (kSoldier_Gfx2), and stands in
WALK = [(11, 12, 13, 12), (4, 5, 6, 5), (0, 1, 2, 3), (7, 8, 9, 10)]
STAND = [11, 4, 0, 7]
_THRUST = [(6, 0), (-6, 0), (0, 6), (0, -6)]


def soldier_pieces(g, head, sword, thrust=0):
    """One soldier pose: head (front), weapon, body; up-facing poses hold the weapon
    behind the body. Small body pieces (the arm and shield) take ALttP palette 4,
    drawn here as colors + 8."""
    face = POSE_FACE[g]
    c, f = _HEAD[head]
    hd = [(0, -_D1_YD[g], 0x100 | c, f, 2, 0)]
    body = []
    for i in (3, 2, 1, 0):
        j = g * 4 + i
        big = _D2_B[j]
        body.append((_D2_X[j], _D2_Y[j], 0x100 | _D2_C[j], _D2_F[j], big, 0 if big else 8))
    dx, dy = (_THRUST[face] if thrust else (0, 0))
    weapon = []
    for i in (1, 0):
        j = g * 2 + i
        weapon.append((_D3_X[j] + dx, _D3_Y[j] + dy, 0x100 | (_D3_C[j] + (3 if sword else 0)), _D3_F[j], 0, 0))
    return hd + (body + weapon if face == 3 else weapon + body)


def build_soldiers():
    V = G['Vram'](36)
    org = (8, 12)
    for kind, sword in (('SOLDIER', True), ('SPEARMAN', False)):
        # walking each way, the head looking the way it goes
        for g in range(14):
            frame('%s_%d' % (kind, g), compose(V, soldier_pieces(g, POSE_FACE[g], sword), org))
        # standing, the head glancing about (ALttP's look-around while it waits)
        for f in range(4):
            for h in range(4):
                if h != f:
                    frame('%s_LOOK_%d_%d' % (kind, f, h), compose(V, soldier_pieces(STAND[f], h, sword), org))
        # added: a start as it spots Link (drawn up tall), the weapon thrust out at
        # him, and a stagger when a blow knocks it back
        for f in range(4):
            stand = compose(V, soldier_pieces(STAND[f], f, sword), org)
            frame('%s_ALERT_%d' % (kind, f), scaled(stand, 0.9, 1.12))
            frame('%s_THRUST_%d' % (kind, f), compose(V, soldier_pieces(WALK[f][1], f, sword, thrust=1), org))
            frame('%s_HURT_%d' % (kind, f), scaled(stand, 1.12, 0.88))
    # the blue soldier is ALttP palette 4 all over; the red spearman palette 3 with
    # palette 4's arms and shield
    p4 = mcpal(4, 37)
    palette('SOLDIER', G['two_palettes'](p4, p4))
    palette('SPEARMAN', G['two_palettes'](mcpal(3, 37), p4))


def build_cannons():
    V = G['Vram'](10)
    org = (8, 8)
    # by the way it fires (0 right, 1 left, 2 down, 3 up): its char at rest and as it
    # fires, and the flip (Sprite_66_WallCannonVerticalLeft)
    chars = [(0x0e, 0x2e), (0x0e, 0x2e), (0x0c, 0x0a), (0x0c, 0x0a)]
    flips = [0x40, 0, 0, 0x80]
    back = [(-2, 0), (2, 0), (0, -2), (0, 2)]
    for d in range(4):
        for k in range(2):
            frame('CANNON_%d_%d' % (d, k), compose(V, [(0, 0, 0x100 | chars[d][k], flips[d], 2, 0)], org))
        # added: the kick back into the wall as it fires
        frame('CANNON_KICK_%d' % d, shifted(compose(V, [(0, 0, 0x100 | chars[d][1], flips[d], 2, 0)], org), *back[d]))
    ball = compose(V, [(0, 0, 0x124, 0, 2, 0)], org)
    frame('CANNONBALL_0', ball)
    # added: the ball turning as it rolls, and flattening as it smashes on a wall
    frame('CANNONBALL_1', mirrored(ball))
    frame('CANNONBALL_2', {(-1 - x, -1 - y): c for (x, y), c in ball.items()})
    frame('CANNONBALL_3', {(x, -1 - y): c for (x, y), c in ball.items()})
    frame('CANNONBALL_SMASH', scaled(ball, 1.3, 0.7))
    palette('CANNON', mcpal(6, 27))
    palette('CANNONBALL', mcpal(4, 27))


def build_tiles():
    V = G['Vram'](19)
    org = (8, 8)
    for g, c in enumerate((0xd3, 0xc3)):
        q = [(0, 0, 0x100 | c, 0, 0, 0), (8, 0, 0x100 | c, 0x40, 0, 0), (0, 8, 0x100 | c, 0x80, 0, 0),
             (8, 8, 0x100 | c, 0xc0, 0, 0)]
        frame('FTILE_%d' % g, compose(V, q, org))
    # added: the tile tipping up out of the floor, and its shards as it breaks
    t = compose(V, [(0, 0, 0x1d3, 0, 0, 0), (8, 0, 0x1d3, 0x40, 0, 0), (0, 8, 0x1d3, 0x80, 0, 0),
                    (8, 8, 0x1d3, 0xc0, 0, 0)], org)
    frame('FTILE_TIP_0', scaled(t, 1.0, 0.5, dy=-4))
    frame('FTILE_TIP_1', scaled(t, 1.0, 0.75, dy=-2))
    shards = {}
    for (x, y), c in t.items():
        sx = -3 if x < 0 else 3
        sy = -3 if y < 0 else 3
        if (x + y) % 3:
            shards[(x + sx, y + sy)] = c
    frame('FTILE_BREAK', shards)
    palette('FTILE', mcpal(0, 40))


def build_sparks():
    V = G['Vram'](12)
    org = (8, 8)
    # the spark spins (its flips change every four frames) and flickers between
    # palettes 1 and 2 (colors + 8)
    for k, add in enumerate((0, 8)):
        for g, f in enumerate((0, 0x40, 0x80, 0xc0)):
            frame('SPARK_%d' % (k * 4 + g), compose(V, [(0, 0, 0x128, f, 2, add)], org))
    # added: the spark fizzling out
    frame('SPARK_FIZZLE', scaled(compose(V, [(0, 0, 0x128, 0, 2, 0)], org), 0.6, 0.6, dy=-3))
    palette('SPARK', G['two_palettes'](mcpal(1, 26), mcpal(2, 26)))


def build(g):
    global G
    G = g
    G['use_sprite'](G['SPRITE_INDEX_11'])
    build_soldiers()
    build_cannons()
    build_tiles()
    build_sparks()
    G['PREVIEW_PALETTES'].update({'SOLDIER': 'SOLDIER', 'SPEARMAN': 'SPEARMAN', 'CANNON': 'CANNON',
                                  'CANNONBALL': 'CANNONBALL', 'FTILE': 'FTILE', 'SPARK': 'SPARK'})
