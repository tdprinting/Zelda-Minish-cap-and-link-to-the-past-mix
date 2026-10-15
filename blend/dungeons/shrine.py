"""
The Temple of Bonds: a dungeon only two players together can clear (the co-op host
runs both games; in single player a guard keeps the way in shut, see blendShrine.c).

Each of its chambers is a room of another temple, A Link to the Past's and the
blend's: the Sanctuary of Hyrule Castle, the Tower of Hera's great hall, a Desert
Palace hall, the Eastern Palace's statue rooms, Skull Woods, the Ice Palace, the
Palace of Darkness, Thieves' Town, the Swamp Palace and Ganon's Tower. Every
chamber keeps its own temple's look: each room has its own tileset (characters
and palette), and the rooms share the area's metatile table, each in its own
stretch of it. The chambers are joined by steps that take Link from one to the
next, as the dungeons' staircases do.

Its puzzles are new and need both players (blendShrine.c runs them, from the table
this writes to custom/gen/shrine.h):
  plates   a pressure plate is down while either Link stands on it. A "twin" group
           solves once all its plates are down at the same time; a "hold" group keeps
           its gates and bridges open only while one of its plates is down.
  gates    iron bars across a way through, open once their bit is solved or while
           their hold group is down. Bridges are the same over a chasm.
  seals    twin seals, crystals in two different chambers: both must be struck
           within a moment of each other, so the players split up and strike as one.
  levers   a crystal that one strike solves for good (to let the partner who held
           a gate open follow).
Solved puzzles are local flags of the temple, and each game tells the other which
it has solved (BlendPlayerSync.shrine), so both games' temples stay alike.

Called by build.py, which writes everything into its .inc files.
"""
import os
import struct

import numpy as np

import alttprooms as A
import celshade
import tileset

NAME = 'TempleOfBonds'
AREA = 'AREA_NULL_3F'
# the cave in South Hyrule Field, west of Link's house (was a cave with a few rupees)
EXIT = ('AREA_HYRULE_FIELD', 'ROOM_HYRULE_FIELD_SOUTH_HYRULE_FIELD', 0x58, 0x128)
# local flags: chests from FLAG_BASE, solved puzzle bits from BIT_FLAG (bit i: flag BIT_FLAG + i)
FLAG_BASE = 0x10
BIT_FLAG = 0x60
BOSS_FLAG = 0x7f

TILE_CHEST = 16
TILE_STAIRS = 622

# Minish Cap items
ITEM = {
    'map': (0x50, 0), 'compass': (0x51, 0), 'rupees20': (0x56, 0), 'rupees50': (0x57, 0),
    'rupees100': (0x58, 0), 'heart_piece': (0x63, 0), 'bombs': (0x5d, 0), 'arrows': (0x5e, 0),
    # the reward: A Link to the Past's sword beam, as Minish Cap's Sword Beam scroll
    'sword_beam': (0x4d, 0),
}

# enemies (tmc/include/enemy.h; 0x67 and up are A Link to the Past's, alttpEnemies.c)
OCTOROK, CHUCHU, LEEVER, ROLLOBITE, DARK_NUT, KEESE = 0x00, 0x01, 0x02, 0x04, 0x05, 0x08
SPINY_CHUCHU, SPEAR_MOBLIN, ROPE, SPINY_BEETLE = 0x0b, 0x15, 0x2c, 0x35
ALTTP_STALFOS, ALTTP_EYEGORE, ALTTP_POPO, ALTTP_ANTIFAIRY, ARMOS_KNIGHT = 0x67, 0x68, 0x69, 0x6a, 0x6b
ALTTP_ROPE, ALTTP_MOBLIN, LANMOLA, ALTTP_LEEVER, ALTTP_BEAMOS = 0x6c, 0x6d, 0x71, 0x73, 0x74
ALTTP_GIBDO, ALTTP_BARI, ALTTP_HARDHAT, ALTTP_BUMPER = 0x77, 0x7a, 0x7b, 0x7c
MINI_HELMASAUR, TERRORPIN, HOVER, ZOL, FIRESNAKE = 0x8b, 0x8c, 0x8d, 0x8f, 0x90
PENGATOR, STALFOS_KNIGHT, WIZZROBE, ZAZAK = 0x9c, 0x9d, 0x9e, 0xa1

# what each device's cells do (blendShrine.c)
KIND_PLATE, KIND_GATE, KIND_SEAL, KIND_LEVER = 1, 2, 3, 4
NONE = 0xff

# solved bits
BIT_ENTRY = 0       # the Sanctuary's twin plates
BIT_DESERT = 1      # the lever behind the Desert hall's held gate
BIT_SKULL = 2       # the lever past Skull Woods's held bridge
BIT_TWIN_SEALS = 3  # the Eastern and Ice seals, struck together
BIT_LEAP = 4        # the Palace of Darkness's leapfrog, finished
BIT_THIEVES = 5     # Thieves' Town's twin seals in one hall (the big chest)
BIT_SWAMP = 6       # the Swamp hall's twin plates (the way to the boss)
BIT_GT_GATE = 7     # (spare)

# plate groups: a twin group's bit, or NONE for a hold group
GROUPS = {
    0: BIT_ENTRY,       # Sanctuary: both plates at once
    1: NONE,            # Desert: hold the gate
    2: NONE,            # Skull Woods: hold the bridge
    3: NONE,            # Palace of Darkness: the first plate of the leapfrog
    4: NONE,            # Palace of Darkness: the second
    5: BIT_SWAMP,       # Swamp: both plates at once, far apart
}
# twin seal pairs: the bit both seals solve
PAIRS = {
    0: BIT_TWIN_SEALS,  # the Eastern Palace's seal and the Ice Palace's
    1: BIT_THIEVES,     # the two seals at the ends of Thieves' Town's hall
}

# the chambers, in order (room 0 is the way in). Cells are (x, y) of the 32x32 plan.
#   steps: (cell, link): a step that takes Link to the other step with the same link
#   plates: (cell, group); gates and bridges: ([cells], bit, hold group)
#   seals: (cell, pair); levers: (cell, bit)
#   chests: (cell, item); big: (cell, item) for the big chest
#   enemies: (enemy, form, cell)
#   opens: rectangles (x0, y0, x1, y1, exclusive) of A Link to the Past's walls opened to floor
ROOMS = [
    dict(
        src=0x12, entrance=0x12, temple='Hyrule Castle: the Sanctuary',
        exit_door=1,  # the south door (DIR_S) leads out to Hyrule Field
        plates=[((11, 21), 0), ((20, 21), 0)],
        gates=[([(x, 10) for x in range(10, 22)], BIT_ENTRY, NONE)],
        steps=[((15, 6), 'hub')],
        enemies=[(KEESE, 0, (9, 18)), (KEESE, 0, (22, 18))],
    ),
    dict(
        src=0x17, entrance=0x77, temple='Tower of Hera: the great hall',
        steps=[((16, 28), 'hub'), ((2, 7), 'desert'), ((26, 7), 'skull'), ((22, 22), 'pod')],
        gates=[([(21, 21), (22, 21), (23, 21), (21, 22), (23, 22), (21, 23), (22, 23), (23, 23)],
                BIT_TWIN_SEALS, NONE)],
        enemies=[(ALTTP_BUMPER, 0, (10, 22)), (ALTTP_HARDHAT, 0, (18, 12)), (ALTTP_HARDHAT, 0, (12, 26))],
    ),
    dict(
        src=0x74, entrance=0x84, temple='Desert Palace: the statue hall',
        steps=[((4, 11), 'desert'), ((19, 29), 'eastern')],
        plates=[((20, 6), 1)],
        gates=[([(7, 16), (8, 16)], NONE, 1), ([(23, 16), (24, 16)], BIT_DESERT, NONE)],
        levers=[((27, 22), BIT_DESERT)],
        enemies=[(ALTTP_LEEVER, 0, (8, 25)), (ALTTP_LEEVER, 0, (22, 29)), (ALTTP_BEAMOS, 0, (6, 8))],
    ),
    dict(
        src=0xd9, entrance=0xc9, temple='Eastern Palace: the statue rooms',
        steps=[((4, 28), 'eastern')],
        seals=[((25, 21), 0)],
        chests=[((8, 21), 'map')],
        enemies=[(ALTTP_EYEGORE, 0, (10, 24)), (ALTTP_STALFOS, 0, (22, 26)), (ALTTP_POPO, 0, (26, 25))],
    ),
    dict(
        src=0x67, entrance=0x57, temple='Skull Woods: the bone garden',
        steps=[((22, 4), 'skull'), ((6, 4), 'ice')],
        plates=[((19, 26), 2)],
        bridges=[([(x, y) for y in (23, 24) for x in range(14, 18)], BIT_SKULL, 2)],
        gates=[([(7, 9), (8, 9)], BIT_SKULL, NONE)],
        levers=[((6, 27), BIT_SKULL)],
        enemies=[(ALTTP_GIBDO, 0, (24, 18)), (ALTTP_STALFOS, 1, (6, 18)), (ZOL, 0, (21, 10))],
    ),
    dict(
        src=0x5e, entrance=0x0e, temple='Ice Palace: the frozen cross',
        steps=[((23, 29), 'ice')],
        seals=[((23, 24), 0)],
        chests=[((27, 4), 'compass')],
        enemies=[(PENGATOR, 0, (8, 24)), (PENGATOR, 0, (10, 26)), (SPINY_CHUCHU, 0, (21, 9))],
    ),
    dict(
        src=0x0b, entrance=0x4a, temple='Palace of Darkness: the leapfrog',
        steps=[((6, 28), 'pod'), ((24, 20), 'thieves')],
        plates=[((5, 22), 3), ((26, 22), 3)],
        gates=[([(15, 23), (15, 24)], BIT_LEAP, 3)],
        levers=[((21, 27), BIT_LEAP)],
        chests=[((24, 4), 'rupees100')],
        enemies=[(TERRORPIN, 0, (24, 25)), (MINI_HELMASAUR, 0, (8, 24)), (DARK_NUT, 0, (19, 9))],
    ),
    dict(
        src=0x64, entrance=0xdb, temple="Thieves' Town: the long hall",
        steps=[((12, 27), 'thieves'), ((29, 24), 'swamp')],
        seals=[((3, 8), 1), ((28, 8), 1)],
        opens=[(7, 14, 9, 17)],
        big=((15, 4), 'sword_beam'),
        # a fence around the big chest, and the way on to the Swamp Palace
        gates=[([(x, 6) for x in range(12, 20)] + [(12, y) for y in (3, 4, 5)] + [(19, y) for y in (3, 4, 5)],
                BIT_THIEVES, NONE),
               ([(16, 23), (16, 24)], BIT_THIEVES, NONE)],
        enemies=[(ALTTP_MOBLIN, 0, (9, 10)), (ALTTP_MOBLIN, 0, (22, 10)), (ROPE, 0, (16, 26))],
    ),
    dict(
        src=0x26, entrance=0x28, temple='Swamp Palace: the drained cistern',
        steps=[((8, 3), 'swamp'), ((15, 29), 'boss')],
        opens=[(15, 29, 17, 31)],
        plates=[((5, 21), 5), ((27, 21), 5)],
        gates=[([(14, 28), (15, 28), (16, 28), (17, 28)], BIT_SWAMP, NONE)],
        enemies=[(HOVER, 0, (10, 24)), (ZOL, 0, (22, 22)), (CHUCHU, 0, (6, 6))],
        chests=[((27, 9), 'rupees50')],
    ),
    dict(
        src=0xa6, entrance=0x0c, temple="Ganon's Tower: the arena",
        boss=True,
        steps=[((23, 3), 'boss')],
        enemies=[(ARMOS_KNIGHT, 0, (10, 10)), (ARMOS_KNIGHT, 0, (14, 10)), (ARMOS_KNIGHT, 0, (18, 10)),
                 (ARMOS_KNIGHT, 0, (10, 14)), (ARMOS_KNIGHT, 0, (14, 14)), (ARMOS_KNIGHT, 0, (18, 14)),
                 (LANMOLA, 0, (16, 22))],
    ),
]

# --- art for the devices, in the blended cel-shaded style (light from the upper left) ---

OUTLINE = tuple(int(v) for v in celshade.OUTLINE)


def _c(rgb):
    return tuple(int(v) for v in celshade.snap(np.array(rgb, dtype=float)))


IRON = _c((76, 80, 104))
IRON_LIT = _c((168, 172, 200))
IRON_DARK = _c((44, 44, 64))
PLATE = _c((112, 124, 160))
PLATE_LIT = _c((176, 188, 224))
PLATE_DARK = _c((64, 72, 104))
PLATE_DOWN = _c((84, 92, 124))
TWIN_MARK = _c((240, 192, 64))
TWIN_GLOW = _c((248, 240, 160))
HOLD_MARK = _c((72, 200, 184))
HOLD_GLOW = _c((176, 248, 232))
CHASM = _c((16, 12, 24))
CHASM_FACE = _c((56, 48, 64))
WOOD = _c((160, 104, 56))
WOOD_LIT = _c((216, 160, 96))
WOOD_DARK = _c((96, 56, 32))
PRECIOUS = [IRON, IRON_LIT, IRON_DARK, PLATE, PLATE_LIT, PLATE_DARK, PLATE_DOWN, TWIN_MARK, TWIN_GLOW,
            HOLD_MARK, HOLD_GLOW, CHASM, CHASM_FACE, WOOD, WOOD_LIT, WOOD_DARK, OUTLINE]


def art_gate(base, closed, edges):
    """Iron bars rising out of the floor (closed), or their sockets in it (open). `edges`:
    which sides of the cell join another gate cell ('l', 'r', 't', 'b')."""
    img = base.copy()
    if not closed:
        for x in (2, 7, 12):
            img[7:9, x:x + 2] = IRON_DARK
            img[9, x:x + 2] = IRON
        return img
    # a top rail and three bars, on a shadow
    img[13:16, 1:15] = np.array(base[13:16, 1:15] * 0.7, dtype=int)
    for x in (2, 7, 12):
        img[1:14, x - 1] = OUTLINE
        img[1:14, x + 2] = OUTLINE
        img[1:14, x:x + 2] = IRON
        img[1:14, x] = IRON_LIT
        img[0, x - 1:x + 3] = OUTLINE
    img[3:6, 0 if 'l' in edges else 1:16 if 'r' in edges else 15] = IRON
    img[3, 0 if 'l' in edges else 1:16 if 'r' in edges else 15] = IRON_LIT
    img[2, 0 if 'l' in edges else 1:16 if 'r' in edges else 15] = OUTLINE
    img[6, 0 if 'l' in edges else 1:16 if 'r' in edges else 15] = OUTLINE
    return img


def art_plate(base, down, twin):
    img = base.copy()
    mark, glow = (TWIN_MARK, TWIN_GLOW) if twin else (HOLD_MARK, HOLD_GLOW)
    img[1:15, 1:15] = OUTLINE
    if down:
        img[2:14, 2:14] = PLATE_DOWN
        img[2, 2:14] = PLATE_DARK
        img[2:14, 2] = PLATE_DARK
    else:
        img[2:13, 2:13] = PLATE
        img[2, 2:13] = PLATE_LIT
        img[2:13, 2] = PLATE_LIT
        img[13, 2:14] = PLATE_DARK
        img[2:14, 13] = PLATE_DARK
    # the mark: two linked diamonds (twin) or one with a bar (hold)
    o = 1 if down else 0
    cy, cx = 7 + o, 7 + o
    pts = [(-2, 0), (-1, -1), (-1, 1), (0, -2), (0, 2), (1, -1), (1, 1), (2, 0)]
    if twin:
        for dx in (-2, 2):
            for dy, ddx in pts:
                img[cy + dy, cx + dx + ddx] = glow if down else mark
    else:
        for dy, ddx in pts:
            img[cy + dy, cx + ddx] = glow if down else mark
        img[cy, cx - 1:cx + 2] = glow if down else mark
    return img


def art_chasm(base, edges):
    img = np.empty_like(base)
    img[:] = CHASM
    if 't' not in edges:
        img[0:4] = CHASM_FACE
        img[0] = OUTLINE
        img[4] = OUTLINE
    if 'l' not in edges:
        img[:, 0] = OUTLINE
    if 'r' not in edges:
        img[:, 15] = OUTLINE
    if 'b' not in edges:
        img[15] = OUTLINE
    return img


def art_bridge(base, edges, vertical):
    img = art_chasm(base, edges)
    if vertical:
        img[:, 2:14] = WOOD
        for y in range(0, 16, 4):
            img[y, 2:14] = WOOD_DARK
            img[y + 1, 2:14] = WOOD_LIT
        img[:, 2] = OUTLINE
        img[:, 13] = OUTLINE
    else:
        img[2:14, :] = WOOD
        for x in range(0, 16, 4):
            img[2:14, x] = WOOD_DARK
            img[2:14, x + 1] = WOOD_LIT
        img[2] = OUTLINE
        img[13] = OUTLINE
    return img


def art_crystal(base, state):
    import alttpgfx
    img = base.copy()
    art = alttpgfx.peg_switch_art()
    if art is None:
        img[3:13, 4:12] = TWIN_MARK if state == 0 else HOLD_MARK
        return img
    rgb, mask = art[state]
    for y in range(16):
        for x in range(16):
            if mask[y][x]:
                img[y, x] = celshade.snap(np.array(rgb[y][x], dtype=float))
    return img


def _edges(cell, cells):
    x, y = cell
    out = ''
    for side, (dx, dy) in (('l', (-1, 0)), ('r', (1, 0)), ('t', (0, -1)), ('b', (0, 1))):
        if (x + dx, y + dy) in cells:
            out += side
    return out


def neighbor_floor(plan, cell, avoid=()):
    """The floor cell beside a step where Link stands on arriving: below it if he can."""
    x, y = cell
    for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0), (0, 2), (1, 1), (-1, 1)):
        c = (x + dx, y + dy)
        if plan.at(*c) == A.FLOOR and c not in avoid:
            return c
    if os.environ.get('SHRINE_PREVIEW'):
        print('WARNING room %03x: no floor beside the step at %s' % (plan.id, cell))
        return cell
    raise RuntimeError('room %03x: no floor beside the step at %s' % (plan.id, cell))


def art_of(blend, m):
    """A metatile's 16x16 picture, from the keys it was made of (for the preview)."""
    for (key, _), idx in blend.meta_b.items():
        if idx == m and key and key[0] != 'mc':
            pix = np.zeros((16, 16, 3), dtype=np.uint8)
            for n, t in enumerate(key):
                for j in range(8):
                    for i in range(8):
                        c = t[j][i]
                        if c is not None:
                            pix[(n // 2) * 8 + j, (n % 2) * 8 + i] = c
            return pix
    return None


def preview(out, k, spec, plan, img, empty, devs, blend):
    """A picture of a chamber as the blend draws it, devices shut, with the cell grid,
    the steps and enemies marked (for laying the chambers out)."""
    from PIL import Image, ImageDraw
    pix = np.array(img, dtype=np.uint8).copy()
    pix[np.array(empty)] = (0, 0, 0)
    for d in devs:
        if d.art[0] is not None:
            x, y = d.cell
            pix[y * 16:y * 16 + 16, x * 16:x * 16 + 16] = d.art[0]
    im = Image.fromarray(pix).convert('RGB')
    dr = ImageDraw.Draw(im, 'RGBA')
    for n in range(0, 32, 2):
        dr.line((n * 16, 0, n * 16, 511), fill=(255, 255, 255, 30))
        dr.line((0, n * 16, 511, n * 16), fill=(255, 255, 255, 30))
        dr.text((n * 16 + 2, 1), str(n), fill=(255, 255, 0))
        dr.text((1, n * 16 + 2), str(n), fill=(0, 255, 255))
    for cell, link in spec.get('steps', ()):
        dr.rectangle((cell[0] * 16, cell[1] * 16, cell[0] * 16 + 15, cell[1] * 16 + 15), outline=(0, 255, 0))
        dr.text((cell[0] * 16 + 2, cell[1] * 16 + 3), link[:3], fill=(0, 255, 0))
    for e, form, c in spec.get('enemies', ()):
        dr.ellipse((c[0] * 16 + 3, c[1] * 16 + 3, c[0] * 16 + 12, c[1] * 16 + 12), outline=(255, 0, 0))
    for c, item in list(spec.get('chests', ())) + ([spec['big']] if spec.get('big') else []):
        dr.rectangle((c[0] * 16 + 2, c[1] * 16 + 2, c[0] * 16 + 13, c[1] * 16 + 13), outline=(255, 0, 255))
    dr.rectangle((0, 494, 300, 511), fill=(0, 0, 0))
    dr.text((3, 497), '%d %s (%03x)' % (k, spec['temple'], spec['src']), fill=(255, 255, 255))
    os.makedirs(out, exist_ok=True)
    im.save(os.path.join(out, 'room%02d.png' % k))
    rows = [[plan.at(x, y) for x in range(32)] for y in range(32)]
    for d in devs:
        rows[d.cell[1]][d.cell[0]] = ('B' if d.kind == KIND_GATE and d.solid == 2 else
                                      {KIND_PLATE: 'P', KIND_GATE: 'G', KIND_SEAL: 'S', KIND_LEVER: 'L'}.get(d.kind, '?'))
    for cell, link in spec.get('steps', ()):
        rows[cell[1]][cell[0]] = '@'
    for c, item in list(spec.get('chests', ())) + ([spec['big']] if spec.get('big') else []):
        rows[c[1]][c[0]] = 'C'
    for e, form, c in spec.get('enemies', ()):
        rows[c[1]][c[0]] = 'e'
    with open(os.path.join(out, 'room%02d.txt' % k), 'w') as f:
        f.write('\n'.join(''.join(r) for r in rows) + '\n')


class Device:
    def __init__(self, kind, cell, a=NONE, b=NONE, solid=0):
        self.kind, self.cell, self.a, self.b, self.solid = kind, cell, a, b, solid
        self.m0 = self.m1 = None


def generate(build, tiler, out_dir, maps_inc, rooms_inc, ents_inc, exits_h, entries_h):
    """Builds the temple's rooms and appends their lines to build.py's outputs. Returns
    the lines of custom/gen/shrine.h."""
    import build as B
    plans, devices, steps = [], [], []
    for k, spec in enumerate(ROOMS):
        out = spec.get('exit_door')
        out = (out,) if isinstance(out, int) and not isinstance(out, bool) else ()
        p = B.Plan(spec['src'], sealed=tuple(d for d in (B.DIR_N, B.DIR_S, B.DIR_W, B.DIR_E) if d not in out),
                   outside=out)
        # every way out of the room is shut but the temple's own way in
        if not spec.get('exit_door'):
            for d in [d for d in p.doors if d['kind'] == 'exit']:
                (x0, y0), (x1, y1) = d['inner'], d['edge']
                for y in range(min(y0, y1), max(y0, y1) + 1):
                    for x in range(min(x0, x1), max(x0, x1) + 1):
                        p.put(x, y, A.SOLID)
                p.doors.remove(d)
        for x0, y0, x1, y1 in spec.get('opens', ()):
            for y in range(y0, y1):
                for x in range(x0, x1):
                    p.put(x, y, A.FLOOR)
        # A Link to the Past's own stairs lead nowhere here: the temple's steps replace them
        p.stairs = []
        seeds = []
        for cell, link in spec.get('steps', ()):
            p.put(cell[0], cell[1], A.FLOOR)
            seeds.append(cell)
        devs = []
        for cell, group in spec.get('plates', ()):
            devs.append(Device(KIND_PLATE, cell, group, GROUPS[group] != NONE))
        for cells, bit, hold in spec.get('gates', ()):
            for c in cells:
                devs.append(Device(KIND_GATE, c, bit, hold, solid=1))
                devs[-1].group = ('gate', tuple(cells))
        for cells, bit, hold in spec.get('bridges', ()):
            for c in cells:
                devs.append(Device(KIND_GATE, c, bit, hold, solid=2))
                devs[-1].group = ('bridge', tuple(cells))
        for cell, pair in spec.get('seals', ()):
            devs.append(Device(KIND_SEAL, cell, pair, NONE, solid=3))
        for cell, bit in spec.get('levers', ()):
            devs.append(Device(KIND_LEVER, cell, bit, NONE, solid=3))
        for d in devs:
            if p.at(*d.cell) == A.SOLID:
                if not os.environ.get('SHRINE_PREVIEW'):
                    raise RuntimeError('%s room %d: device at %s is in a wall' % (NAME, k, d.cell))
                print('WARNING room %d: device at %s is in a wall' % (k, d.cell))
            p.put(d.cell[0], d.cell[1], A.PIT if d.solid == 2 else A.FLOOR)
            seeds.append(d.cell)
        for cell, _ in list(spec.get('chests', ())) + ([spec['big']] if spec.get('big') else []):
            p.put(cell[0], cell[1], A.FLOOR)
            seeds.append(cell)
        p.finish(seeds)
        # a corridor that ran on into a room the temple leaves out ends at the room's border
        for x, y in [c for side in (B.DIR_N, B.DIR_S, B.DIR_W, B.DIR_E) for c in p.edge(side)]:
            if p.at(x, y) == A.FLOOR:
                p.put(x, y, A.SOLID)
        # the temple's own chests and enemies, not A Link to the Past's
        p.chests = [(c[0], c[1], item) for c, item in spec.get('chests', ())]
        p.big_chests = set()
        if spec.get('big'):
            c, item = spec['big']
            p.chests.append((c[0], c[1], item))
            p.big_chests.add(c)
        p.enemies = [(e, form, c[0] * 16 + 8, c[1] * 16 + 8, c) for e, form, c in spec.get('enemies', ())]
        p.steps_up = [cell for cell, _ in spec.get('steps', ())
                      if A.attr_class(p.room.attr[cell[1] * 2 * 64 + cell[0] * 2]) != A.STAIRS]
        for d in devs:
            if p.at(*d.cell) not in (A.FLOOR, A.PIT):
                if not os.environ.get('SHRINE_PREVIEW'):
                    raise RuntimeError('%s room %d: device at %s is in a wall' % (NAME, k, d.cell))
                print('WARNING room %d: device at %s is in a wall' % (k, d.cell))
        plans.append(p)
        devices.append(devs)

    # where each step leads: the step with the same link in another room
    links = {}
    for k, spec in enumerate(ROOMS):
        for cell, link in spec.get('steps', ()):
            links.setdefault(link, []).append((k, cell))
    for link, ends in links.items():
        assert len(ends) == 2, '%s: step %s needs two ends, has %s' % (NAME, link, ends)

    rooms_inc.append('gAreaRoomHeaders_%s::' % NAME)
    tileset_lines = []
    maps_list, area_list, exit_lists = [], [], []
    flag = FLAG_BASE
    table = []
    for k, (spec, p, devs) in enumerate(zip(ROOMS, plans, devices)):
        label = '%s_%02d' % (NAME, k)
        bottom, top, _ = tiler.draw(p.cells, 32, 32)
        bigs = {c: B.big_chest(p, c) for c in p.big_chests}
        for (cx, cy, item) in p.chests:
            if not bigs.get((cx, cy)):
                bottom[cy * 32 + cx] = TILE_CHEST
                top[cy * 32 + cx] = 0
        for cell, _ in spec.get('steps', ()):
            bottom[cell[1] * 32 + cell[0]] = TILE_STAIRS
            top[cell[1] * 32 + cell[0]] = 0
        blend = tileset.Blend('%s_R%02d' % (NAME, k), trim=True)
        new_b, new_t = blend.room(p, bottom, top)
        img, empty = blend.last_look
        blend.precious |= set(PRECIOUS)
        floor_type = max((blend.types_b[bottom[i]] for i in range(1024) if p.cells[i] == A.FLOOR),
                         key=lambda t: sum(1 for i in range(1024) if p.cells[i] == A.FLOOR and blend.types_b[bottom[i]] == t))

        def meta(pix, tile_type):
            em = np.zeros((16, 16), dtype=bool)
            key = tuple(blend.tile_key(pix, em, dx * 8, dy * 8) for dy in (0, 1) for dx in (0, 1))
            return blend.metatile(blend.meta_b, (key, tile_type))

        for d in devs:
            x, y = d.cell
            i = y * 32 + x
            base = np.array(img[y * 16:y * 16 + 16, x * 16:x * 16 + 16], dtype=int)
            base[np.array(empty[y * 16:y * 16 + 16, x * 16:x * 16 + 16])] = OUTLINE
            own = blend.types_b[bottom[i]]
            if d.kind == KIND_PLATE:
                d.m0 = meta(art_plate(base, False, d.b), floor_type)
                d.m1 = meta(art_plate(base, True, d.b), floor_type)
                d.b = NONE
            elif d.kind == KIND_GATE and d.solid == 1:
                e = _edges(d.cell, d.group[1])
                d.m0 = meta(art_gate(base, True, e), tileset.PEG_UP_TYPE)
                d.m1 = meta(art_gate(base, False, e), floor_type)
            elif d.kind == KIND_GATE:
                e = _edges(d.cell, d.group[1])
                vertical = all(c[0] == d.group[1][0][0] for c in d.group[1])
                d.m0 = meta(art_chasm(base, e), own)
                d.m1 = meta(art_bridge(base, e, vertical), floor_type)
                d.solid = 0
            else:
                d.m0 = meta(art_crystal(base, 0), tileset.PEG_UP_TYPE)
                d.m1 = meta(art_crystal(base, 1), tileset.PEG_UP_TYPE)
                d.solid = 0
            new_b[i] = d.m0
            d.art = (art_of(blend, d.m0), art_of(blend, d.m1))
        if os.environ.get('SHRINE_PREVIEW'):
            preview(os.environ['SHRINE_PREVIEW'], k, spec, p, img, empty, devs, blend)
        lines, parts = blend.write_parts(out_dir)
        tileset_lines += lines
        # this chamber's own metatile table (blendShrine.c's Blend_RoomTiles hands the game
        # the current room's, so each chamber has the full 2048 metatiles to itself)
        rlab = lambda key: '(g%s_R%02dTiles_%s - gMapData)' % (NAME, k, key)
        for key, vals in parts.items():
            open(os.path.join(out_dir, '%s_R%02d_%s.bin' % (NAME, k, key)), 'wb').write(
                struct.pack('<%dH' % len(vals), *vals))
            tileset_lines.append('\t.align 2\ng%s_R%02dTiles_%s::\n\t.incbin "custom/gen/%s_R%02d_%s.bin"' % (
                NAME, k, key, NAME, k, key))
        tileset_lines += [
            '\t.align 2',
            'gAreaTiles_%s_R%02d::' % (NAME, k),
            '\ttiles_bottom %s, 0x%x, 0' % (rlab('tiles_bottom'), len(parts['tiles_bottom']) * 2),
            '\ttiles_top %s, 0x%x, 0' % (rlab('tiles_top'), len(parts['tiles_top']) * 2),
            '\ttile_types_bottom %s, 0x%x, 0' % (rlab('types_bottom'), len(parts['types_bottom']) * 2),
            '\ttile_types_top %s, 0x%x, 0, 1' % (rlab('types_top'), len(parts['types_top']) * 2),
        ]
        remap = blend.remap or {}
        cells_b = [m if m >= 0x4000 else remap.get(m, m) for m in new_b]
        cells_t = new_t
        for d in devs:
            d.m0 = remap.get(d.m0, d.m0)
            d.m1 = remap.get(d.m1, d.m1)
            table.append('    { %d, %d, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, %d },' % (
                k, d.kind, d.a, d.b, d.cell[0] + d.cell[1] * 64, d.m0, d.m1, d.solid))
        for layer, cells in (('bottom', cells_b), ('top', cells_t)):
            open(os.path.join(out_dir, '%s_%s.bin' % (label, layer)), 'wb').write(
                struct.pack('<%dH' % len(cells), *cells))
            maps_inc.append('\t.align 2\ng%s_%s::\n\t.incbin "custom/gen/%s_%s.bin"' % (label, layer, label, layer))
        maps_list.append('g%s' % label)
        # each chamber stands apart on the map (the steps join them, not the borders)
        rooms_inc.append('\troom_header 0x%x, 0x0, 0x200, 0x200, %d' % (k * 2 * 32, k))

        # entities: the big chest object, enemies, chests
        ents_inc.append('Entities_%s::' % label)
        chest_flags = {}
        for n, (cx, cy, item) in enumerate(p.chests):
            chest_flags[(cx, cy)] = flag + n
            if bigs.get((cx, cy)):
                bx, by = bigs[(cx, cy)]
                ents_inc.append('\tobject_raw subtype=0x%x, x=0x%x, y=0x%x, paramA=0x4, paramB=0x%x' % (
                    B.OBJ_CHEST_SPAWNER, bx, by, flag + n))
        ents_inc.append('\tentity_list_end\n')
        ents_inc.append('Enemies_%s::' % label)
        for (mc, form, x, y, _) in p.enemies:
            ents_inc.append('\tenemy_raw subtype=0x%x, paramA=0x%x, x=0x%x, y=0x%x' % (mc, form, x, y))
        ents_inc.append('\tentity_list_end\n')
        ents_inc.append('TileEntities_%s::' % label)
        for (cx, cy, item) in p.chests:
            mc, sub = ITEM[item]
            if bigs.get((cx, cy)):
                ents_inc.append('\ttile_entity type=0x3, paramA=0x%x, paramB=0x%x, paramC=0x%x, paramD=0x%x' % (
                    (flag, (sub << 8) | mc) + bigs[(cx, cy)]))
            else:
                ents_inc.append('\ttile_entity type=0x2, paramA=0x%x, paramB=0x%x, paramC=0x%x' % (
                    flag, (sub << 8) | mc, cy * 64 + cx))
            flag += 1
        ents_inc.append('\ttile_entity_list_end\n')
        ents_inc.append('Room_%s::\n\t.4byte Entities_%s, 0, Enemies_%s, TileEntities_%s, 0, 0, 0, 0\n' % (
            label, label, label, label))
        area_list.append('Room_%s' % label)

        # exits: the steps, and the way out
        exits = []
        for cell, link in spec.get('steps', ()):
            (k2, cell2), = [e for e in links[link] if e != (k, cell)]
            land = neighbor_floor(plans[k2], cell2, [d.cell for d in devices[k2]])
            exits.append('\t{ WARP_TYPE_AREA, 0x%x, 0x%x, 0x%x, 0x%x, TRANSITION_SHAPE_AREA_12x12, %s, %d, 1, '
                         'TRANSITION_TYPE_NORMAL, 0x4, 0x0, 0x0, 0x0 },' % (
                             cell[0] * 16 + 8, cell[1] * 16 + 8, land[0] * 16 + 8, land[1] * 16 + 8, AREA, k2))
        for d in p.doors:
            if d['kind'] == 'exit':
                area, room, x, y = EXIT
                exits.append('\t{ WARP_TYPE_BORDER, 0x0, 0x0, 0x%x, 0x%x, %s, %s, %s, 1, '
                             'TRANSITION_TYPE_NORMAL, 0x4, 0x0, 0x0, 0x0 },' % (x, y, B.BORDER[d['dir']], area, room))
                break
        exit_lists.append('const Transition gExitList_%s[] = {\n%s\n\tTransitionListEnd,\n};' % (label, '\n'.join(exits)))
        print('%s room %d (%s, %03x): %d devices' % (NAME, k, spec['temple'], spec['src'], len(devs)))

    assert flag <= BIT_FLAG, '%s: the chest flags run into the puzzle flags' % NAME
    rooms_inc.append('\t.2byte 0xFFFF\n')
    maps_inc.append('')
    maps_inc += tileset_lines
    maps_inc += ['\t.align 2', 'gAreaTileSets_%s::' % NAME]
    maps_inc += ['\t.4byte gAreaTileSet_%s_R%02d_0' % (NAME, k) for k in range(len(ROOMS))]
    # (the area's own table, in tile_headers.s, is the first chamber's: gAreaTiles_<name>_R00)
    maps_inc += ['\t.align 2', 'gShrineRoomTiles::']
    maps_inc += ['\t.4byte gAreaTiles_%s_R%02d' % (NAME, k) for k in range(len(ROOMS))]
    for m in maps_list:
        maps_inc.append('\t.align 2\ngAreaRoomMap_%s::' % m)
        maps_inc.append('\tmap_bottom (%s_bottom - gMapData), 0x800, 0' % m)
        maps_inc.append('\tmap_top (%s_top - gMapData), 0x800, 0, 1' % m)
    maps_inc.append('\t.align 2\ngAreaRoomMaps_%s::' % NAME)
    maps_inc += ['\t.4byte gAreaRoomMap_%s' % m for m in maps_list]
    ents_inc.append('Area_%s::' % NAME)
    ents_inc += ['\t.4byte %s' % a for a in area_list]
    ents_inc.append('')
    exits_h += exit_lists
    exits_h.append('const Transition* const gExitLists_%s[] = {\n%s\n};' % (
        NAME, '\n'.join('\tgExitList_%s_%02d,' % (NAME, k) for k in range(len(ROOMS)))))
    entries_h.append('#define ENTRY_%s 0x%x, 0x%x' % ((NAME,) + B.arrival(plans[0])))
    boss = [k for k, s in enumerate(ROOMS) if s.get('boss')][0]
    groups = ', '.join('0x%x' % GROUPS.get(g, NONE) for g in range(max(GROUPS) + 1))
    pairs = ', '.join('0x%x' % PAIRS.get(g, NONE) for g in range(max(PAIRS) + 1))
    return ['#ifndef BLEND_SHRINE_H', '#define BLEND_SHRINE_H', '',
            '#define SHRINE_AREA %s' % AREA, '#define SHRINE_ROOMS %d' % len(ROOMS),
            '#define SHRINE_BIT_FLAG 0x%x' % BIT_FLAG,
            '#define SHRINE_BOSS_ROOM %d' % boss,
            '#define SHRINE_BOSS_FLAG 0x%x' % BOSS_FLAG, '',
            'enum { SHRINE_PLATE = %d, SHRINE_GATE, SHRINE_SEAL, SHRINE_LEVER };' % KIND_PLATE, '',
            'typedef struct {',
            '    u8 room;',
            '    u8 kind;',
            '    u8 a;     /**< plate: group; gate: bit that opens it for good; seal: pair; lever: bit */',
            '    u8 b;     /**< gate: hold group that opens it while down */',
            '    u16 pos;  /**< tile position */',
            '    u16 meta0; /**< metatile shut (gate), up (plate) or unstruck (seal, lever) */',
            '    u16 meta1; /**< metatile open, down or solved */',
            '    u8 solid; /**< 1: shut, Link can\'t be in it (the gate waits for him to step off) */',
            '    u8 pad;',
            '} ShrineDevice;', '',
            '/** each plate group\'s bit (solved once all its plates are down at once), or 0xff to hold gates */',
            'static const u8 sShrineGroups[] = { %s };' % groups,
            '/** each pair of twin seals\' bit */',
            'static const u8 sShrinePairs[] = { %s };' % pairs, '',
            'static const ShrineDevice sShrineDevices[] = {'] + table + [
            '    { 0xff, 0, 0, 0, 0, 0, 0, 0 },', '};', '', '#endif']
