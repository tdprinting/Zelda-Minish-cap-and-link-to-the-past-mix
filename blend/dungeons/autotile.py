"""
Learns how a Minish Cap dungeon tileset is laid out from the game's own rooms,
then draws any wall/floor/pit/water plan with it.

For every cell of every room in the source areas we note its class ('#' solid,
'.' floor, 'p' pit, '~' water, '+' doorway) and the classes around it. A new
plan is drawn by giving each cell the (bottom, top) metatile pair Minish Cap
used most often for the same surroundings, so wall faces, corners and pit
edges come out the way the original artists drew them.
"""
from collections import Counter, defaultdict

import mctiles

OUTSIDE = '#'


def combined_class(bottom, top, types_b, types_t):
    t = mctiles.cell_class(top, types_t) if top else None
    if t in ('#', 'p', '~', '+'):
        return t
    return mctiles.cell_class(bottom, types_b)


def _at(grid, w, h, x, y):
    if 0 <= x < w and 0 <= y < h:
        return grid[y * w + x] or OUTSIDE
    return OUTSIDE


def keys(grid, w, h, x, y):
    """Neighbourhood keys from the most to the least specific."""
    n = [_at(grid, w, h, x + dx, y + dy) for dy in (-1, 0, 1) for dx in (-1, 0, 1)]
    c = n[4]
    return [
        ('9', ''.join(n)),
        ('5', c + n[1] + n[3] + n[5] + n[7]),
        ('1', c),
    ]


class Tiler:
    def __init__(self, tileset_area, areas):
        self.types_b = mctiles.tile_types(tileset_area, 'bottom')
        self.types_t = mctiles.tile_types(tileset_area, 'top')
        self.table = defaultdict(Counter)
        for area, label in areas:
            headers = mctiles.room_headers(label)
            for room, bottom, top in mctiles.room_maps(area):
                i = int(room.split('_')[0])
                if i >= len(headers):
                    continue
                w, h = headers[i][2], headers[i][3]
                if len(bottom) != w * h or len(top) != w * h:
                    continue
                grid = [combined_class(bottom[k], top[k], self.types_b, self.types_t) for k in range(w * h)]
                for y in range(h):
                    for x in range(w):
                        if grid[y * w + x] is None:
                            continue
                        pair = (bottom[y * w + x], top[y * w + x])
                        if grid[y * w + x] == '.' and pair[1] != 0:
                            # floor drawn on the upper layer (outdoor sand in the Fortress of
                            # Winds): Link walks on the lower layer here, so leave it out
                            continue
                        for k in keys(grid, w, h, x, y):
                            self.table[k][pair] += 1

    def pick(self, grid, w, h, x, y):
        for k in keys(grid, w, h, x, y):
            options = self.table.get(k)
            if options:
                return options.most_common(1)[0][0], k[0]
        return None, '0'

    def draw(self, grid, w, h):
        """Returns bottom and top cell lists, and how many cells matched at each level."""
        bottom, top, stats = [], [], Counter()
        for y in range(h):
            for x in range(w):
                pair, level = self.pick(grid, w, h, x, y)
                stats[level] += 1
                if pair is None:
                    pair = (0, 0)
                bottom.append(pair[0])
                top.append(pair[1])
        return bottom, top, stats
