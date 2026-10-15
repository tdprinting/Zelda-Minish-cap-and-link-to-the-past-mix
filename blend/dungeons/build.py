#!/usr/bin/env python3
"""
Rebuilds A Link to the Past dungeons as Minish Cap areas.

Each dungeon keeps A Link to the Past's room plan (walls, pits, doors, stairs,
chests, enemies), read from the user's own cartridge by zelda3's room loader,
and is drawn with a Minish Cap dungeon tileset by the learned autotiler.

Writes, under tmc/custom/gen/ (git-ignored, since it is derived from the ROM):
  <dungeon>_<n>_bottom.bin / _top.bin   room maps
  maps.inc        incbins and map headers, included at the end of map_data.s
  rooms.inc       room headers, included by room_headers.s
  entities.inc    entity and tile entity lists, included by entity_headers.s
  exits.h         room exits, included by transitions.c

Run before `make CUSTOM=1`. Needs the room dump from zelda3 (see README).
"""
import os
import struct

import yaml

import alttprooms as A
import autotile
import alttpgfx
import tileset

HERE = os.path.dirname(os.path.abspath(__file__))
TMC = os.path.join(HERE, '..', '..', 'tmc')
OUT = os.path.join(TMC, 'custom', 'gen')
YAML_DIR = os.environ.get('ALTTP_YAML', '/home/claude/z3/assets/dungeon')

# Minish Cap metatiles in the Fortress of Winds (Castor Darknut) tileset
TILE_CHEST = 16
TILE_STAIRS = 622

# A Link to the Past chest items -> (Minish Cap item, sub value)
ITEMS = {
    0x24: (0x53, 0),     # small key
    0x32: (0x52, 0),     # big key
    0x33: (0x50, 0),     # map
    0x25: (0x51, 0),     # compass
    0x34: (0x54, 0),     # 1 rupee
    0x35: (0x55, 0),     # 5 rupees
    0x36: (0x56, 0),     # 20 rupees
    0x41: (0x57, 0),     # 50 rupees
    0x40: (0x58, 0),     # 100 rupees
    0x28: (0x5d, 0),     # bombs
    0x44: (0x5e, 0),     # arrows
    0x17: (0x63, 0),     # heart piece
    0x0b: (0x09, 0),     # bow
    # Desert Palace's Power Glove: Minish Cap's Power Bracelets do the same lifting
    0x1b: (0x45, 0),
    # Tower of Hera's Moon Pearl already comes with the Ocarina, so the mountain tower
    # holds the Mirror Shield (A Link to the Past has one too; Biggoron's stays)
    0x1f: (0x0e, 0),
}

# A Link to the Past enemies -> blend enemies: id, or (id, form). Ids 0x67 and up are
# A Link to the Past's own enemies (tmc/src/enemy/alttpEnemies.c); None: left out
ALTTP_STALFOS, ALTTP_EYEGORE, ALTTP_POPO, ALTTP_ANTIFAIRY, ARMOS_KNIGHT = 0x67, 0x68, 0x69, 0x6a, 0x6b
ENEMIES = {
    'A7-Stalfos': ALTTP_STALFOS,
    '105-StalfosDrop': (ALTTP_STALFOS, 1),    # waits in the ceiling and drops on Link
    '118-StalfosAppear': (ALTTP_STALFOS, 1),
    '83-GreenRocklops': ALTTP_EYEGORE,          # zelda3's name for the green Eyegore
    '84-RedRocklops': (ALTTP_EYEGORE, 1),
    '4F-Tentacle': ALTTP_POPO,                  # and for the Popos
    '4E-Tentacle2': ALTTP_POPO,
    '15-Bubble': ALTTP_ANTIFAIRY,
    '82-4Bubbles': ALTTP_ANTIFAIRY,
    '53-ArmosKnight': ARMOS_KNIGHT,
    # Desert Palace and Tower of Hera: Minish Cap stand-ins until their own enemies exist
    '71-Leever': 0x02,
    '63-SandCrab1': 0x04, '64-SandCrab2': 0x04,
    '54-Lanmolas': 0x0f,
    '18-Moldorm': 0x0d, '09-Mouldrum': 0x0d,
    '86-Triceritops': 0x04,
}

DOOR_SMALL_KEY = 0x1c
DOOR_BIG_KEY = 0x1e
DOOR_EXIT = 0x0a
# entrance doors of several sizes, all leading out
DOOR_EXITS = (0x06, 0x0a, 0x0c, 0x0e, 0x10)
# locked doors over a staircase, upper and lower layer
DOOR_STAIR_KEY = (0x20, 0x22, 0x24, 0x26)

DIR_N, DIR_S, DIR_W, DIR_E = 0, 1, 2, 3
# Minish Cap door objects face 0 up, 1 right, 2 down, 3 left
MC_DIR = {DIR_N: 0, DIR_E: 1, DIR_S: 2, DIR_W: 3}

# name: labels; area: the Minish Cap area it fills; rooms: A Link to the Past room ids
# (the first is the entrance, Minish Cap room 0); exit: where the way out leads;
# links: rooms whose door to the outside leads instead to another room's (a walk
# around outside in A Link to the Past); flag_base: first local flag it uses.
DUNGEONS = [
    dict(
        name='EasternPalace',
        rooms=[0xc9, 0xb9, 0xa9, 0xa8, 0xaa, 0x99, 0xb8, 0xba, 0xc8, 0xd8, 0xd9, 0xda],
        entrance=0xc9,
        # the entrance door leads back out to the cave mouth in the Eastern Hills
        area='AREA_EMPTY',
        exit=('AREA_HYRULE_FIELD', 'ROOM_HYRULE_FIELD_EASTERN_HILLS_CENTER', 0xa8, 0xb0),
        flag_base=0x10,
    ),
    dict(
        name='DesertPalace',
        rooms=[0x84, 0x74, 0x73, 0x75, 0x83, 0x85, 0x63, 0x53, 0x43, 0x33],
        entrance=0x84,
        # a cracked cave in the Trilby Highlands (was the Keese chest cave)
        area='AREA_NULL_1B',
        exit=('AREA_HYRULE_FIELD', 'ROOM_HYRULE_FIELD_TRILBY_HIGHLANDS', 0x88, 0x240),
        # the east wing's door leads round the outside to the back entrance
        links={0x85: 0x63, 0x63: 0x85},
        flag_base=0x40,
    ),
    dict(
        name='TowerOfHera',
        rooms=[0x77, 0x87, 0x31, 0x27, 0x17, 0x07],
        entrance=0x77,
        # a cracked cave high on Mt. Crenel (was the Chuchu pot chest cave)
        area='AREA_NULL_1C',
        exit=('AREA_MT_CRENEL', 'ROOM_MT_CRENEL_CENTER', 0x348, 0x70),
        flag_base=0x80,
    ),
]


def read_room(room_id, lower=False):
    path = os.path.join(A.ROOMS, '%03x.bin' % room_id)
    data = open(path, 'rb').read()
    o = 0x2000 + 0x4000 + 5 + 12
    types = struct.unpack('<16H', data[o:o + 32])
    addrs = struct.unpack('<16H', data[o + 32:o + 64])
    dirs = struct.unpack('<16H', data[o + 64:o + 96])
    doors = []
    for t, a, d in zip(types, addrs, dirs):
        if t == 0 and a == 0:
            continue
        word = a >> 1
        if (word >= 0x1000) != lower:
            continue  # the other layer
        word &= 0xfff
        doors.append(dict(type=t & 0xff, x=word % 64, y=word // 64, dir=d & 3))
    return doors


class Plan:
    """One room, at 16px cells, ready to draw."""

    def __init__(self, room_id):
        self.id = room_id
        self.yaml = yaml.safe_load(open(os.path.join(YAML_DIR, 'dungeon-%d.yaml' % room_id)))
        # a room whose doors are all on its second layer has its walls there too (the
        # Tower of Hera's entrance hall); its first layer only scrolls along
        self.lower = not read_room(room_id) and bool(read_room(room_id, True))
        self.room = A.Room(room_id, self.lower)
        self.cells = list(self.room.cells)
        self.doors = []      # (kind, cell x, cell y, dir)
        self.stairs = []     # (cell x, cell y, stair index)
        self.chests = []     # (cell x, cell y, alttp item)
        self.enemies = []    # (mc enemy, form, pixel x, pixel y)
        self._doors()
        self._stairs()

    def finish(self, edges=()):
        """After the rooms are joined: `edges` are cells on the room's border that lead
        straight on into a neighboring room."""
        self._reach(list(edges))
        self._chests()
        self._enemies()
        self._clean()

    def edge(self, side):
        """The cells along one border, in order."""
        return {DIR_N: [(i, 0) for i in range(32)], DIR_S: [(i, 31) for i in range(32)],
                DIR_W: [(0, i) for i in range(32)], DIR_E: [(31, i) for i in range(32)]}[side]

    def at(self, x, y):
        if 0 <= x < 32 and 0 <= y < 32:
            return self.cells[y * 32 + x]
        return '#'

    def put(self, x, y, c):
        if 0 <= x < 32 and 0 <= y < 32:
            self.cells[y * 32 + x] = c

    def _doors(self):
        for d in read_room(self.id, self.lower):
            x, y, dr = d['x'], d['y'], d['dir']
            kind = {DOOR_SMALL_KEY: 'key', DOOR_BIG_KEY: 'boss'}.get(d['type'], 'open')
            if d['type'] in DOOR_EXITS:
                kind = 'exit'
            if d['type'] in DOOR_STAIR_KEY:
                # a locked door in front of stairs: the stairs stay, a key door stands
                # on the locked cell before them
                cx = (x + 1) // 2
                rows = [cy for cy in range(max(0, y // 2 - 2), min(32, y // 2 + 4))
                        if A.LOCKED in (self.at(cx, cy), self.at(cx - 1, cy))]
                if not rows:
                    continue
                for cy in rows:
                    # plain floor: a doorway's tiles would hold Link in the doorway and
                    # keep the stairs from taking him; the door object blocks the way
                    self.put(cx, cy, A.FLOOR)
                # the door on the locked cell nearest the room
                cy = max(rows) if dr == DIR_N else min(rows)
                self.doors.append(dict(kind='key', dir=dr, inner=(cx, cy), edge=None, other=self.id))
                continue
            if 12 < (y if dr in (DIR_N, DIR_S) else x) < 52 and kind in ('key', 'boss') and dr in (DIR_N, DIR_S):
                # a locked door between two halves of the same room: a one-cell way
                # through the wall with the Minish Cap door in it
                cx, cy = (x + 1) // 2, y // 2
                for j in range(-3, 4):
                    for c in (cx - 1, cx + 1):
                        if self.at(c, cy + j) in '+ksE':
                            self.put(c, cy + j, '#')
                    if self.at(cx, cy + j) not in (A.FLOOR, A.PIT, A.WATER, A.STAIRS):
                        self.put(cx, cy + j, '+')
                inner = (cx, cy + 1) if dr == DIR_N else (cx, cy)
                self.doors.append(dict(kind=kind, dir=dr, inner=inner, edge=None, other=self.id))
                continue
            if 12 < (y if dr in (DIR_N, DIR_S) else x) < 52:
                # a door between two halves of the same room: open the wall there
                cx, cy = x // 2, y // 2
                span = [(cx + i, cy + j) for i in (0, 1) for j in range(-2, 3)] if dr in (DIR_N, DIR_S) else \
                       [(cx + j, cy + i) for i in (0, 1) for j in range(-2, 3)]
                for p in span:
                    if self.at(*p) not in (A.PIT, A.WATER, A.STAIRS):
                        self.put(p[0], p[1], A.FLOOR)
                continue
            if dr in (DIR_N, DIR_S):
                cx = (x + 1) // 2
                rows = range(0, y // 2 + 2) if dr == DIR_N else range(y // 2, 32)
                for cy in rows:
                    for c in (cx - 1, cx, cx + 1):
                        if self.at(c, cy) in '+ksE':
                            self.put(c, cy, '#')
                    self.put(cx, cy, '+')
                # the door cell nearest the room's inside
                inner = (cx, max(rows)) if dr == DIR_N else (cx, min(rows))
                if kind == 'exit':
                    # walls along the way out, so the open ground outside stays out
                    for cy in rows:
                        if cy != inner[1]:
                            for c in (cx - 1, cx + 1):
                                if self.at(c, cy) in (A.FLOOR, A.ENTRANCE):
                                    self.put(c, cy, '#')
            else:
                cy = (y + 1) // 2
                cols = range(0, x // 2 + 2) if dr == DIR_W else range(x // 2, 32)
                for cx in cols:
                    for c in (cy - 1, cy, cy + 1):
                        if self.at(cx, c) in '+ksE':
                            self.put(cx, c, '#')
                    # the tileset has no side doorway Link can walk through, so a plain passage
                    self.put(cx, cy, A.FLOOR)
                inner = (max(cols), cy) if dr == DIR_W else (min(cols), cy)
            edge = (cx, 31 if dr == DIR_S else 0) if dr in (DIR_N, DIR_S) else (31 if dr == DIR_E else 0, cy)
            self.doors.append(dict(kind=kind, dir=dr, inner=inner, edge=edge, other=self.id + STEP[dr]))

    def _stairs(self):
        attr = self.room.attr
        seen = set()
        for cy in range(32):
            for cx in range(32):
                if self.cells[cy * 32 + cx] != A.STAIRS:
                    continue
                if (cx, cy) in seen:
                    continue
                # flood the cluster
                stack, cluster = [(cx, cy)], []
                while stack:
                    p = stack.pop()
                    if p in seen or self.at(*p) != A.STAIRS:
                        continue
                    seen.add(p)
                    cluster.append(p)
                    stack += [(p[0] + 1, p[1]), (p[0] - 1, p[1]), (p[0], p[1] + 1), (p[0], p[1] - 1)]
                index = 0
                for (x, y) in cluster:
                    for dy in (0, 1):
                        for dx in (0, 1):
                            a = attr[(y * 2 + dy) * 64 + x * 2 + dx]
                            if 0x30 <= a <= 0x37:
                                index = a & 3
                for p in cluster:
                    self.put(p[0], p[1], A.FLOOR)
                x, y = min(cluster, key=lambda p: (p[1], p[0]))
                self.stairs.append((x, y, index))

    def _reach(self, edges):
        """A Link to the Past marks the unseen space around a room as floor; Minish Cap's
        camera shows all of it, so wall off any floor Link can't get to from a door or stairs."""
        seeds = [d['inner'] for d in self.doors] + [(x, y) for (x, y, _) in self.stairs] + edges
        if not seeds:
            return
        seen, stack = set(), list(seeds)
        while stack:
            p = stack.pop()
            if p in seen or not (0 <= p[0] < 32 and 0 <= p[1] < 32):
                continue
            if self.at(*p) in (A.SOLID, A.TORCH) and p not in seeds:
                continue
            seen.add(p)
            stack += [(p[0] + 1, p[1]), (p[0] - 1, p[1]), (p[0], p[1] + 1), (p[0], p[1] - 1)]
        for i, c in enumerate(self.cells):
            if c == A.FLOOR and (i % 32, i // 32) not in seen:
                self.cells[i] = A.SOLID

    def _chests(self):
        items = self.yaml.get('Chests') or []
        found = {}
        for layer in (self.room.attr, self.room.lower):
            for cy in range(32):
                for cx in range(32):
                    for dy in (0, 1):
                        for dx in (0, 1):
                            a = layer[(cy * 2 + dy) * 64 + cx * 2 + dx]
                            if 0x58 <= a <= 0x5d and (a - 0x58) not in found:
                                found[a - 0x58] = (cx, cy)
        for n, item in enumerate(items):
            if isinstance(item, str):
                item = int(item.rstrip('!'))
            if n in found and self.at(*found[n]) in (A.FLOOR, A.CHEST, A.BIG_CHEST):
                cx, cy = found[n]
            else:
                # a chest that appears later in A Link to the Past: set it in the middle of the floor
                cx, cy = self.middle()
            self.put(cx, cy, A.FLOOR)
            self.chests.append((cx, cy, item))
        # keys an enemy dropped in A Link to the Past wait in a chest where it stood
        # and so do keys lying on the floor, on a statue Link dashes into, or under a pot
        loose = []
        for s in self.yaml.get('Sprites') or []:
            if len(s) > 4 and s[4] in ('drop_key', 'drop_big_key'):
                loose.append((s[0], s[1], 0x24 if s[4] == 'drop_key' else 0x32))
            elif s[3] in ('E4-Key', '3B-DashItem'):
                loose.append((s[0], s[1], 0x24))
        for s in self.yaml.get('Secrets') or []:
            if s[2] == '08-Key':
                loose.append((s[0] // 2, s[1] // 2, 0x24))
        for x, y, item in loose:
            if self.at(x, y) != A.FLOOR or any((x, y) == c[:2] for c in self.chests):
                # the nearest free floor
                spots = [(i % 32, i // 32) for i, c in enumerate(self.cells) if c == A.FLOOR and
                         not any((i % 32, i // 32) == q[:2] for q in self.chests)]
                if not spots:
                    continue
                x, y = min(spots, key=lambda p: abs(p[0] - x) + abs(p[1] - y))
            self.chests.append((x, y, item))

    def middle(self):
        floor = [(i % 32, i // 32) for i, c in enumerate(self.cells) if c == A.FLOOR]
        mx = sum(p[0] for p in floor) / len(floor)
        my = sum(p[1] for p in floor) / len(floor)
        return min(floor, key=lambda p: (p[0] - mx) ** 2 + (p[1] - my) ** 2)

    def _enemies(self):
        for s in self.yaml.get('Sprites') or []:
            mc = ENEMIES.get(s[3])
            if mc is None:
                continue
            x, y = s[0], s[1]
            if self.at(x, y) != A.FLOOR:
                continue
            form = 0
            if isinstance(mc, tuple):
                mc, form = mc
            self.enemies.append((mc, form, x * 16 + 8, y * 16 + 8))

    def _clean(self):
        """Reduce A Link to the Past's extra classes to the ones the tileset draws."""
        for i, c in enumerate(self.cells):
            if c in (A.LOCKED, A.SHUTTER, A.ENTRANCE):
                self.cells[i] = '#'
            elif c in (A.BLOCK, A.POT, A.CHEST, A.BIG_CHEST, A.STAIRS):
                self.cells[i] = A.FLOOR
            elif c == A.TORCH:
                self.cells[i] = '#'


STEP = {DIR_N: -16, DIR_S: 16, DIR_W: -1, DIR_E: 1}


OPPOSITE = {DIR_N: DIR_S, DIR_S: DIR_N, DIR_W: DIR_E, DIR_E: DIR_W}


def neighbor(r, side):
    if side == DIR_W and r & 15 == 0 or side == DIR_E and r & 15 == 15:
        return None
    return r + STEP[side]


def open_edges(plans):
    """Where A Link to the Past runs a room straight on into the next with no door:
    {room: [border cells that lead on]}."""
    edges = {r: [] for r in plans}
    for r, p in plans.items():
        for side in (DIR_N, DIR_S, DIR_W, DIR_E):
            other = neighbor(r, side)
            if other not in plans:
                continue
            q = plans[other]
            for a, b in zip(p.edge(side), q.edge(OPPOSITE[side])):
                if p.at(*a) == A.FLOOR and q.at(*b) == A.FLOOR:
                    edges[r].append(a)
    return edges


def layout(plans, edges):
    """Room positions in room units. A Link to the Past stacks floors on one grid, but
    Minish Cap scrolls between any rooms that touch, so each floor (rooms joined by
    doors or open edges) gets its own stretch of the map."""
    floor = {}
    for start in plans:
        if start in floor:
            continue
        n = len(set(floor.values()))
        stack = [start]
        while stack:
            r = stack.pop()
            if r in floor:
                continue
            floor[r] = n
            for d in plans[r].doors:
                other = d['other']
                if other in plans and d['kind'] != 'exit':
                    stack.append(other)
            for (x, y) in edges[r]:
                side = DIR_N if y == 0 else DIR_S if y == 31 else DIR_W if x == 0 else DIR_E
                stack.append(neighbor(r, side))
    col0 = min(r & 15 for r in plans)
    row0 = min(r >> 4 for r in plans)
    width = max(r & 15 for r in plans) - col0 + 2
    return {r: ((r & 15) - col0 + floor[r] * width, (r >> 4) - row0) for r in plans}


BORDER = {DIR_N: 'TRANSITION_SHAPE_BORDER_NORTH', DIR_S: 'TRANSITION_SHAPE_BORDER_SOUTH',
          DIR_W: 'TRANSITION_SHAPE_BORDER_WEST', DIR_E: 'TRANSITION_SHAPE_BORDER_EAST'}


def arrival(plan):
    """Where Link stands coming in through a room's door to the outside: just inside it."""
    for d in plan.doors:
        if d['kind'] == 'exit':
            x, y = d['inner']
            dx, dy = {DIR_N: (0, 1), DIR_S: (0, -1), DIR_W: (1, 0), DIR_E: (-1, 0)}[d['dir']]
            return (x + dx) * 16 + 8, (y + dy) * 16 + 8
    raise RuntimeError('room %03x has no door to the outside' % plan.id)


def generate():
    os.makedirs(OUT, exist_ok=True)
    tiler = autotile.Tiler('043', [('088', 'FortressOfWinds'), ('043', 'CastorDarknut')])
    maps_inc, rooms_inc, ents_inc, exits_h, entries_h = [], [], [], [], []
    for dg in DUNGEONS:
        name = dg['name']
        ids = dg['rooms']
        assert ids[0] == dg['entrance'], '%s: the entrance must be the first room' % name
        plans = {r: Plan(r) for r in ids}
        edges = open_edges(plans)
        for r in ids:
            plans[r].finish(edges[r])
        index = {r: i for i, r in enumerate(ids)}
        place = layout(plans, edges)
        flag = dg['flag_base']
        door_flags = {}

        blend = tileset.Blend(name) if os.path.exists(os.path.join(alttpgfx.GFX, '%03x.gfx' % ids[0])) else None
        if blend is None:
            print('%s: no A Link to the Past room graphics, drawing with Minish Cap tiles' % name)

        rooms_inc.append('gAreaRoomHeaders_%s::' % name)
        maps_list = []
        area_list = []
        exit_lists = []
        for r in ids:
            p = plans[r]
            i = index[r]
            bottom, top, _ = tiler.draw(p.cells, 32, 32)
            for (cx, cy, item) in p.chests:
                bottom[cy * 32 + cx] = TILE_CHEST
                top[cy * 32 + cx] = 0
            for (cx, cy, _) in p.stairs:
                bottom[cy * 32 + cx] = TILE_STAIRS
                top[cy * 32 + cx] = 0
            if blend:
                bottom, top = blend.room(p, bottom, top)
            for layer, cells in (('bottom', bottom), ('top', top)):
                open(os.path.join(OUT, '%s_%02d_%s.bin' % (name, i, layer)), 'wb').write(
                    struct.pack('<%dH' % len(cells), *cells))
                maps_inc.append('\t.align 2\ng%s_%02d_%s::\n\t.incbin "custom/gen/%s_%02d_%s.bin"' % (
                    name, i, layer, name, i, layer))
            maps_list.append('g%s_%02d' % (name, i))
            rooms_inc.append('\troom_header 0x%x, 0x%x, 0x200, 0x200, 0' % (
                place[r][0] * 32, place[r][1] * 32))

            # entities
            label = '%s_%02d' % (name, i)
            ents_inc.append('Entities_%s::' % label)
            for d in p.doors:
                if d['kind'] in ('key', 'boss'):
                    other = d['other']
                    key = (min(r, other), max(r, other), d['dir'] in (DIR_N, DIR_S))
                    if other == r:
                        # inside one room: each door by where it stands
                        key += (d['inner'][0],)
                    if key not in door_flags:
                        door_flags[key] = flag
                        flag += 1
                    x, y = d['inner']
                    sub = 0x8 if d['kind'] == 'key' else 0x39
                    param_a = (0x8 | MC_DIR[d['dir']]) if d['kind'] == 'key' else MC_DIR[d['dir']]
                    ents_inc.append('\tobject_raw subtype=0x%x, x=0x%x, y=0x%x, paramA=0x%x, paramC=0x%x' % (
                        sub, x * 16 + 8, y * 16 + 8, param_a, (door_flags[key] << 16) | 0xffff))
            ents_inc.append('\tentity_list_end\n')
            ents_inc.append('Enemies_%s::' % label)
            for (mc, form, x, y) in p.enemies:
                ents_inc.append('\tenemy_raw subtype=0x%x, paramA=0x%x, x=0x%x, y=0x%x' % (mc, form, x, y))
            ents_inc.append('\tentity_list_end\n')
            ents_inc.append('TileEntities_%s::' % label)
            for (cx, cy, item) in p.chests:
                mc, sub = ITEMS.get(item, (0x56, 0))
                ents_inc.append('\ttile_entity type=0x2, paramA=0x%x, paramB=0x%x, paramC=0x%x' % (
                    flag, (sub << 8) | mc, cy * 64 + cx))
                flag += 1
            ents_inc.append('\ttile_entity_list_end\n')
            ents_inc.append('Room_%s::\n\t.4byte Entities_%s, 0, Enemies_%s, TileEntities_%s, 0, 0, 0, 0\n' % (
                label, label, label, label))
            area_list.append('Room_%s' % label)

            # exits: stairs, and the way out
            exits = []
            for (cx, cy, j) in p.stairs:
                dest = p.room.travel[j + 1] if j + 1 < len(p.room.travel) else 0
                if dest not in plans:
                    continue
                q = plans[dest]
                land = None
                # when two staircases join the same rooms, the first in one room (top to
                # bottom) leads to the first in the other
                mine = sorted((y, x) for (x, y, k) in p.stairs if p.room.travel[k + 1] == dest)
                theirs = sorted((dy, dx) for (dx, dy, dj) in q.stairs if q.room.travel[dj + 1] == r)
                if theirs:
                    n = mine.index((cy, cx))
                    land = theirs[min(n, len(theirs) - 1)][::-1]
                if land is None and q.stairs:
                    land = q.stairs[0][:2]
                if land is None:
                    continue
                lx, ly = land
                # step off the stairs onto the floor below them
                for ox, oy in ((0, 1), (0, -1), (1, 0), (-1, 0), (0, 2)):
                    if q.at(lx + ox, ly + oy) == A.FLOOR:
                        lx, ly = lx + ox, ly + oy
                        break
                exits.append('\t{ WARP_TYPE_AREA, 0x%x, 0x%x, 0x%x, 0x%x, TRANSITION_SHAPE_AREA_12x12, %s, %d, 1, '
                             'TRANSITION_TYPE_NORMAL, 0x4, 0x0, 0x0, 0x0 },' % (
                                 cx * 16 + 8, cy * 16 + 8, lx * 16 + 8, ly * 16 + 8, dg['area'], index[dest]))
            for d in p.doors:
                if d['kind'] != 'exit':
                    continue
                link = dg.get('links', {}).get(r)
                if link is not None:
                    # straight on to the linked room's own door to the outside
                    x, y = arrival(plans[link])
                    area, room = dg['area'], index[link]
                else:
                    area, room, x, y = dg['exit']
                exits.append('\t{ WARP_TYPE_BORDER, 0x0, 0x0, 0x%x, 0x%x, %s, %s, %s, 1, '
                             'TRANSITION_TYPE_NORMAL, 0x4, 0x0, 0x0, 0x0 },' % (
                                 x, y, BORDER[d['dir']], area, room))
                break
            exit_lists.append('const Transition gExitList_%s[] = {\n%s\n\tTransitionListEnd,\n};' % (
                label, '\n'.join(exits)))

        rooms_inc.append('\t.2byte 0xFFFF\n')
        maps_inc.append('')
        if blend:
            maps_inc += blend.write(OUT)
        else:
            # the Minish Cap tiles the rooms were drawn with
            cd = lambda k: '(gAreaTileSet%s_CastorDarknut_%s - gMapData)' % k
            maps_inc += ['\t.align 2\ngAreaTileSets_%s::\n\t.4byte gAreaTileSet_CastorDarknut_0' % name,
                         'gAreaTiles_%s::' % name,
                         '\ttiles_bottom %s, 0x3868, 1' % cd(('', 'bottom')),
                         '\ttiles_top %s, 0x3850, 1' % cd(('', 'top')),
                         '\ttile_types_bottom %s, 0xE1A, 1' % cd(('Types', 'bottom')),
                         '\ttile_types_top %s, 0xE14, 1, 1' % cd(('Types', 'top'))]
        for i, m in enumerate(maps_list):
            maps_inc.append('\t.align 2\ngAreaRoomMap_%s::' % m)
            maps_inc.append('\tmap_bottom (%s_bottom - gMapData), 0x800, 0' % m)
            maps_inc.append('\tmap_top (%s_top - gMapData), 0x800, 0, 1' % m)
        maps_inc.append('\t.align 2\ngAreaRoomMaps_%s::' % name)
        maps_inc += ['\t.4byte gAreaRoomMap_%s' % m for m in maps_list]
        ents_inc.append('Area_%s::' % name)
        ents_inc += ['\t.4byte %s' % a for a in area_list]
        ents_inc.append('')
        exits_h += exit_lists
        exits_h.append('const Transition* const gExitLists_%s[] = {\n%s\n};' % (
            name, '\n'.join('\tgExitList_%s_%02d,' % (name, i) for i in range(len(ids)))))
        entries_h.append('#define ENTRY_%s 0x%x, 0x%x' % ((name,) + arrival(plans[dg['entrance']])))
        print('%s: %d rooms, flags 0x%x-0x%x' % (name, len(ids), dg['flag_base'], flag - 1))
        print('  ' + ', '.join('%d: %03x' % (i, r) for i, r in enumerate(ids)))

    def write(fname, lines):
        open(os.path.join(OUT, fname), 'w').write(
            '@ generated by blend/dungeons/build.py from the user\'s own cartridge; do not commit\n'.replace(
                '@', '//' if fname.endswith('.h') else '@') + '\n'.join(lines) + '\n')

    write('maps.inc', maps_inc)
    write('rooms.inc', rooms_inc)
    write('entities.inc', ents_inc)
    write('exits.h', exits_h)
    # where each dungeon's way in from the overworld lands (for transitions.c)
    write('entries.h', entries_h)


if __name__ == '__main__':
    import sys
    if sys.argv[1:] == ['--rooms']:
        # for make-dungeons.sh: each dungeon's entrance room, then its rooms
        for dg in DUNGEONS:
            print(' '.join('%x' % r for r in [dg['entrance']] + dg['rooms']))
    else:
        generate()
