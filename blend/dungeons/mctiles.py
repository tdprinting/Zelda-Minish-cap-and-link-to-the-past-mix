"""
Reads Minish Cap tile data from the tmc build: room maps, metatile types, and
the tables that turn a tile type into collision and an "act tile" (pit, water,
door...). Everything is read from the user's own build directory.
"""
import os
import re
import struct

TMC = os.path.join(os.path.dirname(__file__), '..', '..', 'tmc')
ASSETS = os.path.join(TMC, 'build', 'USA', 'assets', 'maps', 'areas')


def _c_table(path, name):
    """Values of a const u8 table written as NAME_<n> entries."""
    src = open(os.path.join(TMC, path)).read()
    start = src.index(name + '[] = {')
    body = src[start:src.index('};', start)]
    return [int(v) for v in re.findall(r'_(\d+)\s*,', body)]


COLLISION = _c_table('src/playerItem/playerItemBow.c', 'gMapTileTypeToCollisionData')
ACT = _c_table('src/collision.c', 'gMapTileTypeToActTile')

ACT_WATER = (0x0f, 0x10)
ACT_PIT = 0x19
ACT_DOOR = (0x28, 0x29)


def u16s(path):
    data = open(path, 'rb').read()
    return list(struct.unpack('<%dH' % (len(data) // 2), data))


def area_dir(prefix):
    for d in os.listdir(ASSETS):
        if d.startswith(prefix):
            return os.path.join(ASSETS, d)
    raise FileNotFoundError(prefix)


def tile_types(tileset_area, layer):
    d = area_dir(tileset_area)
    name = [f for f in os.listdir(os.path.join(d, 'tileSet'))
            if f.startswith('gAreaTileSetTypes_') and f.endswith('_%s.bin' % layer)][0]
    return u16s(os.path.join(d, 'tileSet', name))


def room_maps(area):
    """Yields (room folder, bottom cells, top cells) for each room of an area."""
    d = os.path.join(area_dir(area), 'rooms')
    for room in sorted(os.listdir(d)):
        files = os.listdir(os.path.join(d, room))
        try:
            bottom = [f for f in files if f.endswith('_bottom.bin')][0]
            top = [f for f in files if f.endswith('_top.bin')][0]
        except IndexError:
            continue
        yield room, u16s(os.path.join(d, room, bottom)), u16s(os.path.join(d, room, top))


def cell_class(tile, types):
    """'#' solid, 'p' pit, '~' water, '+' doorway, '.' floor, or None for special tiles."""
    if tile >= 0x4000 or tile >= len(types):
        return None
    t = types[tile]
    if t >= len(COLLISION):
        return None
    act = ACT[t] if t < len(ACT) else 0
    if act == ACT_PIT:
        return 'p'
    if act in ACT_WATER:
        return '~'
    if act in ACT_DOOR:
        return '+'
    if COLLISION[t] != 0:
        return '#'
    return '.'


def room_headers(label):
    """[(map_x, map_y, width, height, tileset)] in tiles for gAreaRoomHeaders_<label>."""
    src = open(os.path.join(TMC, 'data', 'map', 'room_headers.s')).read()
    start = src.index('gAreaRoomHeaders_%s::' % label)
    out = []
    for line in src[start:].split('\n')[1:]:
        line = line.strip()
        if not line.startswith('room_header'):
            break
        x, y, w, h, ts = [int(v, 0) for v in line.split(None, 1)[1].split(',')]
        out.append((x, y, w // 16, h // 16, ts))
    return out
