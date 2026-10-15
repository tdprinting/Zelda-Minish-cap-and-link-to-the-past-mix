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
        self.travel = list(data[0x2000 + 0x4000:0x2000 + 0x4000 + 5])
        self.cells = self._downsample()

    def _downsample(self):
        cells = []
        for cy in range(32):
            for cx in range(32):
                quad = [attr_class(self.attr[(cy * 2 + dy) * 64 + cx * 2 + dx]) for dy in (0, 1) for dx in (0, 1)]
                pick = None
                for p in PRIORITY:
                    if p in quad:
                        pick = p
                        break
                if pick is None:
                    if self.id in THIN_WALL_ROOMS:
                        pick = FLOOR if quad.count(FLOOR) == 4 else SOLID
                    else:
                        pick = FLOOR if quad.count(FLOOR) >= 2 else SOLID
                cells.append(pick)
        return cells

    def text(self):
        return '\n'.join(''.join(self.cells[y * 32:(y + 1) * 32]) for y in range(32))
