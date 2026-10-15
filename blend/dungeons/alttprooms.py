"""
Reads A Link to the Past dungeon rooms dumped by tool/dumprooms (zelda3's own
room loader run headlessly over the user's ROM) and turns each one into a
32x32 plan of 16px cells: the scale Minish Cap's Link walks on.
"""
import os

ROOMS = os.environ.get('ALTTP_ROOMS', '/home/claude/z3/rooms')

FLOOR = '.'
SOLID = '#'
PIT = 'p'
WATER = '~'
DOOR = '+'      # open doorway
LOCKED = 'k'    # small key door
SHUTTER = 's'   # shutter door, opens when the room is cleared
CHEST = 'C'
BIG_CHEST = 'B'
STAIRS = 'S'
BLOCK = 'b'     # pushable block
POT = 'o'
TORCH = 't'
ENTRANCE = 'E'  # the door out to the overworld


def attr_class(a):
    if a in (0x00, 0x09, 0x1c) or 0x0e <= a <= 0x0f or 0x68 <= a <= 0x6b:
        # 0x1c: floor on the upper layer
        return FLOOR
    if a == 0x08:
        return WATER
    if a == 0x20 or 0xb0 <= a <= 0xbd:
        return PIT
    if 0x58 <= a <= 0x5d:
        return CHEST
    if 0x30 <= a <= 0x39 or a in (0x5e, 0x5f):
        return STAIRS  # stairs to another room
    if a in (0x22, 0x1d, 0x1e, 0x1f, 0x3d, 0x3e, 0x3f):
        return FLOOR   # steps within the room
    if a in (0x66, 0x67):
        return FLOOR   # crystal switch pegs: Minish Cap has no crystal switches, so they stay down
    if 0x70 <= a <= 0x7f:
        return BLOCK
    if 0x50 <= a <= 0x56:
        return POT
    if 0xc0 <= a <= 0xcf:
        return TORCH
    if a in (0x8e, 0x8f):
        return ENTRANCE
    if 0x80 <= a <= 0x8d:
        return DOOR
    if 0x90 <= a <= 0xaf:
        return SHUTTER
    if 0xf0 <= a <= 0xff:
        return LOCKED
    return SOLID


# when the four 8px tiles of a 16px cell disagree, the first class present wins
# rooms whose walls are one 8px tile thick (the Palace of Darkness's dark maze): a cell
# with any wall in it is wall, or the maze would fall away at Minish Cap's 16px scale
THIN_WALL_ROOMS = {0x19}
# rooms ringed by spikes one 8px tile thick (Skull Woods's Mothula lair): a cell with a
# spike in it is wall too, so Link can't stand on them where Minish Cap has none
SPIKE_RING_ROOMS = {0x29}
SPIKE_ATTRS = (0x44,)
# rooms where the blend walks on what A Link to the Past makes a hazard: the Ice Palace's
# room 0x3E hides a spike maze in its floor (undrawn, and Minish Cap has no such spikes),
# Vitreous waits in a pool of goo (0x0D) in Misery Mire's room 0x90, and the Ice Palace's
# rooms 0x1E and 0x4E have floor switches (0x24) across their only way through
FLOOR_ATTR_ROOMS = {0x3e: SPIKE_ATTRS, 0x90: (0x0d,), 0x1e: (0x24,), 0x4e: (0x24,)}
# rooms whose way down is on the lower layer, under an upper floor that covers the room
# (Misery Mire's room 0xA2): the blend takes the staircase up into the plan
LOWER_STAIRS_ROOMS = {0xa2}
# Turtle Rock's rooms with Somaria tracks or pipes over their pits (see Room.tracks)
TRACK_ROOMS = {0xd6, 0xc6, 0xc7, 0xb5, 0xb4, 0x14, 0x15}
# rooms with walkways a tile off the cells' grid (see Room._downsample)
WALKWAY_ROOMS = {0xc5, 0xd5, 0x4e}
# floor A Link to the Past never draws, beside a room's walls (8px tiles: x0, y0, x1, y1,
# exclusive): Misery Mire's room 0x97 has a bare, spiked slab east of its narrow hall
WALL_RECTS = {0x97: [(44, 19, 57, 55)]}

PRIORITY = [ENTRANCE, LOCKED, SHUTTER, DOOR, BIG_CHEST, CHEST, STAIRS, TORCH, BLOCK, POT, PIT, WATER]


class Room:
    def __init__(self, room_id, lower=False):
        data = open(os.path.join(ROOMS, '%03x.bin' % room_id), 'rb').read()
        self.id = room_id
        self.attr = data[:0x1000]
        self.lower = data[0x1000:0x2000]
        if lower:
            # a room whose walls and doors are on the second layer (the first only
            # scrolls along, as in the Tower of Hera's entrance hall): plan that one
            self.attr, self.lower = self.lower, self.attr
        if room_id in FLOOR_ATTR_ROOMS:
            self.attr = bytes(0x1c if a in FLOOR_ATTR_ROOMS[room_id] else a for a in self.attr)
        if room_id in LOWER_STAIRS_ROOMS:
            self.attr = bytes(b if attr_class(b) == STAIRS and attr_class(a) == FLOOR else a
                              for a, b in zip(self.attr, self.lower))
        for x0, y0, x1, y1 in WALL_RECTS.get(room_id, ()):
            attr = bytearray(self.attr)
            for t in range(4096):
                if x0 <= t % 64 < x1 and y0 <= t // 64 < y1:
                    attr[t] = 0x01
            self.attr = bytes(attr)
        self.travel = list(data[0x2000 + 0x4000:0x2000 + 0x4000 + 5])
        # Turtle Rock's Somaria tracks (0xB0-0xBE) run over pits; Minish Cap has no
        # platform to ride them, so the blend lays a bridge along each track instead (and
        # along the pipes of rooms 0x14 and 0x15, which carry Link the same way)
        self.tracks = {(t // 64 // 2) * 32 + t % 64 // 2 for t in range(4096)
                       if 0xb0 <= self.attr[t] <= 0xbe} if room_id in TRACK_ROOMS else set()
        # where a track stops a cell short of the ledge the platform sets Link down on,
        # the bridge runs on to it
        def quad(j):
            return [attr_class(self.attr[(j // 32 * 2 + dy) * 64 + j % 32 * 2 + dx]) for dy in (0, 1) for dx in (0, 1)]
        for i in list(self.tracks):
            for d in (1, -1, 32, -32):
                j, k = i + d, i + 2 * d
                if 0 <= k < 1024 and abs(k % 32 - i % 32) == (2 if d in (1, -1) else 0) and \
                        j not in self.tracks and k not in self.tracks and \
                        all(c == PIT for c in quad(j)) and all(c == FLOOR for c in quad(k)):
                    self.tracks.add(j)
        self.cells = self._downsample()

    def _downsample(self):
        cells = []
        for cy in range(32):
            for cx in range(32):
                quad = [attr_class(self.attr[(cy * 2 + dy) * 64 + cx * 2 + dx]) for dy in (0, 1) for dx in (0, 1)]
                pick = FLOOR if cy * 32 + cx in self.tracks else None
                for p in PRIORITY if pick is None else ():
                    if p in quad:
                        pick = p
                        break
                if pick is None:
                    if self.id in THIN_WALL_ROOMS | SPIKE_RING_ROOMS:
                        pick = FLOOR if quad.count(FLOOR) == 4 else SOLID
                    else:
                        pick = FLOOR if quad.count(FLOOR) >= 2 else SOLID
                cells.append(pick)
        # walkways two tiles wide across a pit, a tile off the cells' grid (Turtle Rock's
        # rooms 0xC5 and 0xD5, the Ice Palace's 0x4E), would be all pit: there, a pit
        # cell is read a tile further east (or south), and is floor where that is floor
        # all round
        if self.id in WALKWAY_ROOMS:
            for i, c in enumerate(cells):
                for x, y in ((i % 32 * 2 + 1, i // 32 * 2), (i % 32 * 2, i // 32 * 2 + 1)):
                    if c == PIT and x + 1 < 64 and y + 1 < 64 and all(
                            attr_class(self.attr[(y + dy) * 64 + x + dx]) == FLOOR for dy in (0, 1) for dx in (0, 1)):
                        cells[i] = FLOOR
        return cells

    def text(self):
        return '\n'.join(''.join(self.cells[y * 32:(y + 1) * 32]) for y in range(32))
