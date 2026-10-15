"""
Calmer art for the blended dungeons: fixes for the spots where A Link to the Past's
art, redrawn on Minish Cap's 16px grid, comes out busy or broken (see tileset.Blend).
"""
import numpy as np

import alttprooms as A

VOID = (40, 32, 24)
PASSABLE = (A.FLOOR, '+')
# how far (pixels) the darkness around a room eats into the tiles of its walls
# dungeons drawn as approved before these fixes, kept exactly as they are
KEEP_AS_IS = ('EasternPalace',)


def _corner(m):
    """The pixels of an 8px tile's corner a 45-degree diagonal cuts off, if `m` (pixels
    of the pattern around the room) is such a corner, give or take a few pixels."""
    yy, xx = np.mgrid[0:8, 0:8]
    best = None
    for d in (xx + yy, (7 - xx) + yy, xx + (7 - yy), (7 - xx) + (7 - yy)):
        for k in range(1, 15):
            tri = d < k
            miss = int((tri != m).sum())
            if best is None or miss < best[0]:
                best = (miss, tri)
    miss, tri = best
    return tri if miss <= 3 and tri.sum() >= 3 else None


def void_fringe(plan, drawn, fill, img, empty, stoned, dungeon=None):
    """A Link to the Past fills the space around its rooms with a pattern; the blend
    darkens it a whole 8px tile or 16px cell at a time, which leaves the pattern in the
    corners of the tiles a diagonal wall cuts across: a stair-step of light around the
    wall. Those corners go dark too. drawn: the room as A Link to the Past draws it;
    fill: the pixels of the pattern that went dark."""
    if not fill.any() or dungeon in KEEP_AS_IS:
        return
    cols = np.unique(drawn[fill].astype(np.int32), axis=0)
    key = lambda a: (a[..., 0].astype(np.int64) << 16) | (a[..., 1].astype(np.int64) << 8) | a[..., 2].astype(np.int64)
    pattern = np.isin(key(drawn.astype(np.int32)), key(cols))
    walk = np.array([c in PASSABLE for c in plan.cells]).reshape(32, 32).repeat(16, axis=0).repeat(16, axis=1)
    dark = np.all(img == VOID, axis=-1)

    def beside(ty, tx):
        return dark[max(0, ty * 8 - 1):ty * 8 + 9, max(0, tx * 8 - 1):tx * 8 + 9].any()

    def go_dark(t, m):
        img[t][m] = VOID
        empty[t][m] = False
        stoned[t][m] = False
        dark[t][m] = True

    # whole tiles of the pattern beside the darkness (a 16px cell half wall, half outside)
    for _ in range(2):
        for ty in range(64):
            for tx in range(64):
                t = (slice(ty * 8, ty * 8 + 8), slice(tx * 8, tx * 8 + 8))
                if not dark[t].all() and not walk[t].any() and (pattern[t] | dark[t]).all() and beside(ty, tx):
                    go_dark(t, ~dark[t])
    # and the corners of the tiles a diagonal wall cuts: the pattern's pixels joined to the
    # darkness within the tile, where they make a corner cut off at 45 degrees
    for ty in range(64):
        for tx in range(64):
            t = (slice(ty * 8, ty * 8 + 8), slice(tx * 8, tx * 8 + 8))
            if dark[t].all() or walk[t].any() or not beside(ty, tx):
                continue
            ok = (pattern[t] | dark[t]) & ~empty[t]
            seen = np.zeros((8, 8), dtype=bool)
            big = dark[max(0, ty * 8 - 1):ty * 8 + 9, max(0, tx * 8 - 1):tx * 8 + 9]
            oy, ox = ty * 8 - max(0, ty * 8 - 1), tx * 8 - max(0, tx * 8 - 1)
            stack = [(y, x) for y in range(8) for x in range(8) if ok[y, x] and (
                dark[t][y, x] or any(0 <= y + oy + dy < big.shape[0] and 0 <= x + ox + dx < big.shape[1] and
                                     big[y + oy + dy, x + ox + dx] for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1))))]
            while stack:
                y, x = stack.pop()
                if seen[y, x]:
                    continue
                seen[y, x] = True
                for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                    if 0 <= ny < 8 and 0 <= nx < 8 and ok[ny, nx] and not seen[ny, nx]:
                        stack.append((ny, nx))
            if not seen.any() or seen.all():
                continue
            tri = _corner(seen)
            if tri is not None:
                go_dark(t, seen & ~dark[t])
    # what is left of it in the steps of a diagonal wall's outer edge: a tile with darkness
    # on two neighboring sides (the steps of 8px tiles round an octagon) goes dark beyond
    # the 45-degree line across it, so the wall's edge runs straight instead of in a
    # stair-step light rim (the Ice Palace's octagons, Thieves' Town's 0x65)
    yy, xx = np.mgrid[0:8, 0:8]
    full = lambda ty, tx: not (0 <= ty < 64 and 0 <= tx < 64) or dark[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8].all()
    for ty in range(64):
        for tx in range(64):
            t = (slice(ty * 8, ty * 8 + 8), slice(tx * 8, tx * 8 + 8))
            if dark[t].all() or walk[t].any():
                continue
            up, down, left, right = full(ty - 1, tx), full(ty + 1, tx), full(ty, tx - 1), full(ty, tx + 1)
            for a, b, d in ((up, left, xx + yy), (up, right, (7 - xx) + yy),
                            (down, left, xx + (7 - yy)), (down, right, (7 - xx) + (7 - yy))):
                if a and b and not (up and down) and not (left and right):
                    beyond = (d < 7) & pattern[t] & ~empty[t] & ~dark[t]
                    if beyond.any():
                        go_dark(t, beyond)


# a floor color this saturated glares (only the Ice Palace's ice has one); it is laid in
# a muted tone of its hue instead
GLARE_SATURATION = 0.55
CALM_SATURATION = 0.28
CALM_LIGHTNESS = 0.56


# where the dungeon's own floors are greys, a floor this saturated stands out as well:
# Ganon's Tower's yellow sand under the Lanmolas (0x6C), laid in a pale, dusty tone
DUNGEON_GLARE = {'GanonsTower': (0.3, 0.16)}


def slab_color(base, size, dungeon=None):
    """The color a floor's stone slabs are laid in: its own, unless that is a glaring
    one (the Ice Palace's slippery floors, a bright cyan in Minish Cap's light)."""
    import colorsys
    r, g, b = (float(v) / 255 for v in base)
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    glare, calm_s = DUNGEON_GLARE.get(dungeon, (GLARE_SATURATION, CALM_SATURATION))
    if s > glare:
        # the same hue, muted: still icy, no longer glowing
        import celshade
        r, g, b = colorsys.hls_to_rgb(h, min(l, CALM_LIGHTNESS), calm_s)
        return celshade.snap(np.array((r * 255, g * 255, b * 255)))
    return base


# A Link to the Past's conveyor belts: floor that carries Link along; Minish Cap has none
CONVEYOR_ATTRS = (0x68, 0x69, 0x6a, 0x6b)


def stone(base):
    """Calm stone slabs in one color, as tileset.Blend.restone lays them."""
    import celshade
    yy, xx = np.mgrid[0:512, 0:512]
    out = np.empty((512, 512, 3))
    out[:] = base
    out[((xx // 16 + yy // 16) % 2) == 1] = celshade.snap(base * 0.95)
    out[(xx % 16 == 0) | (yy % 16 == 0)] = celshade.snap(base + (255 - base) * 0.16)
    out[(xx % 16 == 15) | (yy % 16 == 15)] = celshade.snap(base * 0.82)
    return out


def flatten_conveyors(plan, img, empty, stoned, slab_base):
    """The blend has no conveyor belts, so their striped art (Misery Mire's rows of
    chevrons and dark ribbed bands) is only a busy floor: it is laid in the stone slabs
    of the floor beside it. slab_base: each slab pixel's floor color."""
    attr = np.frombuffer(bytes(plan.room.attr), dtype=np.uint8).reshape(64, 64)
    walk = np.array([c in PASSABLE for c in plan.cells]).reshape(32, 32).repeat(2, axis=0).repeat(2, axis=1)
    # (belts A Link to the Past leaves undrawn are laid in slate as drained basins: see
    # tileset.Blend.paint)
    todo = {(y, x) for y in range(64) for x in range(64) if attr[y, x] in CONVEYOR_ATTRS and walk[y, x]
            and not stoned[y * 8:y * 8 + 8, x * 8:x * 8 + 8].all() and empty[y * 8:y * 8 + 8, x * 8:x * 8 + 8].mean() <= 0.5}
    if not todo:
        return
    belts = set(todo)
    base = {}
    for y in range(64):
        for x in range(64):
            if (y, x) not in todo and stoned[y * 8:y * 8 + 8, x * 8:x * 8 + 8].all():
                base[(y, x)] = tuple(slab_base[y * 8 + 4, x * 8 + 4])
    # each belt tile takes the color of the slabs nearest it, spreading in from the floor
    while todo:
        step = {}
        for (y, x) in todo:
            near = [base[q] for q in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)) if q in base]
            if near:
                step[(y, x)] = max(set(near), key=near.count)
        if not step:
            break
        base.update(step)
        todo -= set(step)
    done = {}
    for (y, x), b in base.items():
        if (y, x) in belts:
            done.setdefault(b, []).append((y, x))
    for b, tiles in done.items():
        pat = stone(np.array(b))
        for y, x in tiles:
            t = (slice(y * 8, y * 8 + 8), slice(x * 8, x * 8 + 8))
            img[t] = pat[t]
            empty[t] = False
            stoned[t] = True
            slab_base[t] = b


def floor_sources(plan, cells, floor, dungeon=None):
    """The floor cell each of `cells` (pots, blocks, doorways and the like the blend
    draws as plain floor) copies its art from: the nearest floor cell walking through
    them, so a pot in a corner takes the floor it stands on rather than a floor across
    a wall that lies nearer (the Palace of Darkness's room 0x2B). Cells walking reaches
    no floor from take the nearest one."""
    nearest = lambda c: min(floor, key=lambda p: abs(p[0] - c[0]) + abs(p[1] - c[1]))
    out = {}
    if dungeon in KEEP_AS_IS:
        for i in cells:
            out[i] = nearest((i % 32, i // 32))
        return out
    todo = set(cells)
    floor_set = set(floor)
    src = {}
    frontier = []
    for i in cells:
        c = (i % 32, i // 32)
        nb = [q for q in ((c[0] + 1, c[1]), (c[0] - 1, c[1]), (c[0], c[1] + 1), (c[0], c[1] - 1)) if q in floor_set]
        if nb:
            src[i] = nearest(c) if nearest(c) in nb else nb[0]
            frontier.append(i)
    while frontier:
        nxt = []
        for i in frontier:
            x, y = i % 32, i // 32
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                j = ny * 32 + nx
                if 0 <= nx < 32 and 0 <= ny < 32 and j in todo and j not in src:
                    src[j] = src[i]
                    nxt.append(j)
        frontier = nxt
    for i in cells:
        out[i] = src.get(i) or nearest((i % 32, i // 32))
    return out


def thin_tracks(plan, turn_cost=3):
    """Turtle Rock's Somaria tracks and pipes become bridges over the pits (see
    alttprooms.Room.tracks); where A Link to the Past packs tracks 8px apart (rooms 0xB5
    and 0x14), Minish Cap's 16px cells run the bridges together into a busy lattice. Only
    the bridges Link needs are kept: from the room's first door, the straightest way
    along the tracks to each ledge, door and stair in turn, sharing the way already
    laid. The rest of the tracks are pit again."""
    import heapq
    walk = lambda i: plan.cells[i] in PASSABLE
    track = {i for i in plan.room.tracks if walk(i)}
    if not track:
        return
    # the ledges: floor off the tracks, each stretch of it one place to get to
    comp, comps = {}, []
    for i in range(1024):
        if walk(i) and i not in track and i not in comp:
            stack, part = [i], []
            comp[i] = len(comps)
            while stack:
                j = stack.pop()
                part.append(j)
                for k in _nbrs(j):
                    if walk(k) and k not in track and k not in comp:
                        comp[k] = len(comps)
                        stack.append(k)
            comps.append(part)
    # doors and stairs on the tracks themselves are places of their own
    for d in plan.doors:
        x, y = d['inner']
        i = y * 32 + x
        if i in track and i not in comp:
            comp[i] = len(comps)
            comps.append([i])
    for x, y, _ in plan.stairs:
        i = y * 32 + x
        if i in track and i not in comp:
            comp[i] = len(comps)
            comps.append([i])
    if len(comps) < 2:
        return
    first = None
    for d in plan.doors:
        x, y = d['inner']
        if y * 32 + x in comp:
            first = comp[y * 32 + x]
            break
    joined = {first if first is not None else 0}
    kept = set()
    while True:
        # Dijkstra from everything joined so far, along tracks, to a place not yet joined
        tree = {j for c in joined for j in comps[c]} | kept
        dist, heap = {}, []
        for j in tree:
            for k in _nbrs(j):
                if k in track and k not in tree:
                    d = k - j
                    heapq.heappush(heap, (1, k, d, None))
        prev = {}
        goal = None
        while heap:
            cost, j, d, p = heapq.heappop(heap)
            if (j, d) in dist:
                continue
            dist[(j, d)] = cost
            prev[(j, d)] = p
            near = [comp[k] for k in _nbrs(j) if k in comp and comp[k] not in joined]
            if j in comp and comp[j] not in joined:
                near.append(comp[j])
            if near:
                goal = ((j, d), near[0])
                break
            for k in _nbrs(j):
                if k in track and k not in tree:
                    nd = k - j
                    heapq.heappush(heap, (cost + 1 + (turn_cost if nd != d else 0), k, nd, (j, d)))
        if goal is None:
            # the rest lie apart from these: start again from one of them
            rest = [c for c in range(len(comps)) if c not in joined and
                    any(k in track for j in comps[c] for k in _nbrs(j))]
            if not rest:
                break
            joined.add(rest[0])
            continue
        state, c = goal
        while state is not None:
            kept.add(state[0])
            state = prev[state]
        joined.add(c)
    keep = kept | {j for c in joined for j in comps[c] if j in track}
    plan.thinned = track - keep
    for i in track - keep:
        plan.cells[i] = A.PIT
        plan.room.cells[i] = A.PIT
    plan.room.tracks = {i for i in plan.room.tracks if i in keep or not walk(i)}


def _nbrs(i):
    x, y = i % 32, i // 32
    for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
        if 0 <= nx < 32 and 0 <= ny < 32:
            yield ny * 32 + nx


def bare_pits(plan, img, empty, stoned):
    """The pits of Turtle Rock's track rooms in plain black, as A Link to the Past's
    other pits are: the pipe room 0x14 patterns its pit in blue, and the tracks the
    blend leaves out (thin_tracks) would still show their dotted lines across it."""
    if plan.id not in A.TRACK_ROOMS:
        return
    walk = [c in PASSABLE for c in plan.cells]
    for t in range(4096):
        tx, ty = t % 64, t // 64
        a = plan.room.attr[t]
        i = (ty // 2) * 32 + tx // 2
        # (and what stood on a track the blend leaves out: Turtle Rock's skull pots)
        if walk[i] or not (A.attr_class(a) == A.PIT or 0xb0 <= a <= 0xbe or
                           i in getattr(plan, 'thinned', ())):
            continue
        tile = (slice(ty * 8, ty * 8 + 8), slice(tx * 8, tx * 8 + 8))
        img[tile] = 0
        empty[tile] = True
        stoned[tile] = False


def over_pit(plan, empty, x, y):
    """Whether cell (x, y) is bare pit beside one of Turtle Rock's bridges, which gets an
    edge where it meets it, as a walkway does where it meets the dark."""
    return plan.id in A.TRACK_ROOMS and plan.cells[y * 32 + x] not in PASSABLE and \
        empty[y * 16:y * 16 + 16, x * 16:x * 16 + 16].mean() > 0.5


# Ganon's Tower's halls round a great pit (see PERSPECTIVE): their walls stand across it
PIT_HALLS = {0x5c, 0x95, 0xa5, 0x4d}


def walls_round_pits(plan, pits, reach=2):
    """In Turtle Rock's track rooms the pit fills most of the room, so the walls round it
    lie far from the bridges: the cells within `reach` of the pits the blend keeps
    (tileset.Blend.unreached) keep their art too, or the walls would show gaps."""
    if plan.id not in A.TRACK_ROOMS | PIT_HALLS or not pits:
        return set()
    near = set(pits)
    for _ in range(reach):
        near |= {j for i in near for j in _nbrs(i)}
    return near


PLAIN_FLOOR_ATTRS = (0x00, 0x09, 0x1c, 0x01, 0x02)


def specks(plan, img, empty, stoned, slab_base, dungeon=None, size=2):
    """Specks of art left on a stone floor: the ends and joins of the thin rails A Link to
    the Past draws on the room's other layer (Misery Mire's rooms 0xC2 and 0x93), which
    the blend's one floor lets Link walk across. A scrap of one or two 8px tiles,
    stone all round, where Link walks, is laid in the stone around it."""
    if dungeon in KEEP_AS_IS:
        return
    walk = np.array([c in PASSABLE for c in plan.cells]).reshape(32, 32).repeat(2, axis=0).repeat(2, axis=1)
    # (plain floor, or the stub of a thin wall the plan let Link walk through: Misery
    # Mire's spike traps, attribute 0x60, keep their spikes)
    walk &= np.isin(np.frombuffer(bytes(plan.room.attr), dtype=np.uint8).reshape(64, 64), PLAIN_FLOOR_ATTRS)
    full = np.array([[stoned[y * 8:y * 8 + 8, x * 8:x * 8 + 8].all() for x in range(64)] for y in range(64)])
    seen = np.zeros((64, 64), dtype=bool)
    for sy in range(64):
        for sx in range(64):
            if full[sy, sx] or seen[sy, sx] or not walk[sy, sx]:
                continue
            part, stack, ok = [], [(sy, sx)], True
            seen[sy, sx] = True
            while stack:
                y, x = stack.pop()
                part.append((y, x))
                for ny in (y - 1, y, y + 1):
                    for nx in (x - 1, x, x + 1):
                        if not (0 <= ny < 64 and 0 <= nx < 64):
                            ok = False
                        elif not full[ny, nx] and not seen[ny, nx]:
                            seen[ny, nx] = True
                            stack.append((ny, nx))
            if not ok or len(part) > size or not all(walk[y, x] for y, x in part):
                continue
            around = [tuple(slab_base[ny * 8 + 4, nx * 8 + 4]) for y, x in part
                      for ny in (y - 1, y, y + 1) for nx in (x - 1, x, x + 1) if full[ny, nx]]
            base = max(set(around), key=around.count)
            if sum(base) == 0:
                continue
            pat = stone(np.array(base))
            for y, x in part:
                t = (slice(y * 8, y * 8 + 8), slice(x * 8, x * 8 + 8))
                img[t] = pat[t]
                empty[t] = False
                stoned[t] = True
                slab_base[t] = base


# A Link to the Past draws the walls of a room around a great pit (Ganon's Tower's long
# halls 0x5C and 0x95, the soldiers' room 0xA5, Moldorm's hall 0x4D, the entrance hall's
# sunken middle 0x0C) as walls seen falling away at a slant, in dithered characters that the
# blend's banded colors turn into busy vertical stripes. Those characters, by dungeon
# (character numbers of its room graphics): the wall face, the lip where it meets the pit,
# and the pit's own sides, which fall into the dark
PERSPECTIVE = {
    'GanonsTower': dict(face=(0x043, 0x044, 0x046, 0x047, 0x050, 0x051, 0x053, 0x054, 0x060, 0x061),
                        lip=(0x048, 0x070, 0x071),
                        side=(0x045, 0x06a, 0x06b, 0x06c, 0x07a, 0x07b,
                              # and the mesh A Link to the Past draws far below Moldorm's
                              # ring (0x4D), which the blend's pits leave plain black
                              0x0c1)),
}


def front_chars(gfx):
    """The character each 8px tile shows most of (its front layer's, or the back one's
    where the front is blank there)."""
    out = []
    for i in range(4096):
        hi, lo = gfx.bg1[i], gfx.bg2[i]
        shown = gfx.composite(i, unmask=False)
        th, tl = gfx.tile(hi), gfx.tile(lo)
        n_hi = sum(1 for y in range(8) for x in range(8) if shown[y][x] and shown[y][x] == th[y][x])
        n_lo = sum(1 for y in range(8) for x in range(8) if shown[y][x] and shown[y][x] == tl[y][x])
        out.append((hi if n_hi > n_lo else lo) & 0x3ff)
    return out


def flat_perspective(plan, gfx, img, empty, dungeon):
    """Calms those slanted walls (PERSPECTIVE): the face in one flat tone of its own
    color, the lip a lighter line along the pit, and the pit's sides as the pit itself,
    plain black. Returns the mask of the face and lip pixels (they cast no outline
    stripes of their own: tileset leaves them out of the soft shading's edges)."""
    spec = PERSPECTIVE.get(dungeon)
    soft = np.zeros((512, 512), dtype=bool)
    if not spec:
        return soft
    chars = front_chars(gfx)
    tiles = {k: [i for i in range(4096) if chars[i] in spec[k]] for k in ('face', 'lip', 'side')}
    if not tiles['face'] and not tiles['side']:
        return soft
    import celshade
    sl = lambda i: (slice(i // 64 * 8, i // 64 * 8 + 8), slice(i % 64 * 8, i % 64 * 8 + 8))
    pix = [img[sl(i)][~empty[sl(i)]] for i in tiles['face']]
    pix = np.concatenate(pix) if pix else np.zeros((0, 3))
    if len(pix):
        # the face's own color, a little darker than its median: a wall in shade
        face = celshade.snap(np.median(pix, axis=0) * 0.9)
        lip = celshade.snap(face + (255 - face) * 0.22)
        for k, tone in (('face', face), ('lip', lip)):
            for i in tiles[k]:
                img[sl(i)] = tone
                empty[sl(i)] = False
                soft[sl(i)] = True
    for i in tiles['side']:
        if A.attr_class(plan.room.attr[i]) != A.PIT:
            continue  # (the same characters lay a floor elsewhere: room 0x3D's mesh)
        img[sl(i)] = 0
        empty[sl(i)] = True
    return soft


BEYOND_WALLS = {'GanonsTower'}


def beyond_walls(plan, img, empty, stoned, dungeon=None):
    """A Link to the Past's floor running round outside a room's walls, on its other layer
    (Ganon's Tower's room 0x5B), is the space around the room too: the blend's stone slabs
    there read as a checkered border. It is the slabs (8px tiles) whose own layer has no
    floor, joined to the darkness around the room."""
    # (only Ganon's Tower: elsewhere the slabs round a room are its floor, and voiding
    # them eats into Thieves' Town's and Misery Mire's rooms)
    if dungeon not in BEYOND_WALLS:
        return
    attr = np.frombuffer(bytes(plan.room.attr), dtype=np.uint8).reshape(64, 64)
    dark = np.all(img == VOID, axis=-1)
    tile = lambda y, x: (slice(y * 8, y * 8 + 8), slice(x * 8, x * 8 + 8))
    # (in a cell Link walks in, only the tiles beside the floor A Link to the Past lays in
    # it: where a room is planned from a layer with no floor at all, Link walks the other's,
    # Ganon's Tower's entrance hall)
    flo = np.vectorize(lambda a: A.attr_class(a) == A.FLOOR)(attr)
    cell_floor = flo.reshape(32, 2, 32, 2).any(axis=(1, 3))
    walk = [c in PASSABLE for c in plan.cells]
    cand = np.array([[stoned[tile(y, x)].mean() > 0.5 and not flo[y, x] and
                      (not walk[(y // 2) * 32 + x // 2] or cell_floor[y // 2, x // 2])
                      for x in range(64)] for y in range(64)])
    seen = np.zeros((64, 64), dtype=bool)
    for sy in range(64):
        for sx in range(64):
            if not cand[sy, sx] or seen[sy, sx]:
                continue
            part, stack, touch = [], [(sy, sx)], False
            seen[sy, sx] = True
            while stack:
                y, x = stack.pop()
                part.append((y, x))
                for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                    if not (0 <= ny < 64 and 0 <= nx < 64):
                        continue
                    if dark[tile(ny, nx)].mean() > 0.5:
                        touch = True
                    elif cand[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
            if touch and len(part) >= 8:
                for y, x in part:
                    img[tile(y, x)] = VOID
                    empty[tile(y, x)] = False
                    stoned[tile(y, x)] = False


# A Link to the Past's spike floor (tile attribute 0x0D), which hurts Link as he walks it;
# the blend's rooms wall it off (Minish Cap has no such floor)
SPIKE_FLOOR = 0x0d
# and rooms whose spike floor is A Link to the Past's other spikes (0x44), laid the same:
# Trinexx's lair 0xA4 has red beds of them across its two upper corners
SPIKE_FLOOR_ROOMS = {0xa4: (0x0d, 0x44)}


def spike_beds(plan, img, empty, stoned, slab_base, dungeon=None):
    """Spike floor, drawn in A Link to the Past as a busy patterned carpet (orange bricks in
    Ganon's Tower, pink in Misery Mire, a brown patch in the Ice Palace's 0x5F), is laid as a
    calm bed of spikes in the room's own stone: a sunken slab with one spike in each 16px
    cell, lit from the upper left. Link can't walk it, as it is walled off in the plan."""
    if dungeon in KEEP_AS_IS:
        return
    import celshade
    walk = [c in PASSABLE for c in plan.cells]
    kinds = SPIKE_FLOOR_ROOMS.get(plan.id, (SPIKE_FLOOR,))
    tiles = [t for t in range(4096) if plan.room.attr[t] in kinds and not walk[(t // 64 // 2) * 32 + t % 64 // 2]]
    if not tiles:
        return
    spiked = np.zeros((512, 512), dtype=bool)
    for t in tiles:
        spiked[t // 64 * 8:t // 64 * 8 + 8, t % 64 * 8:t % 64 * 8 + 8] = True
    # the room's floor: its commonest slab color, away from the spikes
    lay = stoned & ~spiked & (slab_base.sum(axis=-1) > 0)
    if lay.any():
        cols, counts = np.unique(slab_base[lay].reshape(-1, 3), axis=0, return_counts=True)
        base = cols[counts.argmax()]
    else:
        base = np.median(img[~empty & ~spiked], axis=0)
    yy, xx = np.mgrid[0:16, 0:16]
    d = np.abs(xx - 7.5) + np.abs(yy - 7.5)
    cell = np.empty((16, 16, 3))
    cell[:] = celshade.snap(base * 0.62)
    spike = d <= 5
    cell[spike & (xx + yy < 15)] = celshade.snap(base * 1.0 + (255 - base) * 0.1)
    cell[spike & (xx + yy >= 15)] = celshade.snap(base * 0.42)
    cell[(xx == 7) & (yy == 7)] = celshade.snap(base + (255 - base) * 0.4)
    pat = np.tile(cell, (32, 32, 1))
    img[spiked] = pat[spiked]
    empty[spiked] = False
    stoned[spiked] = True
    slab_base[spiked] = base


def spike_edges(plan, img, empty, stoned, dungeon=None):
    """Spike tiles in a cell Link walks (a strip's edge half a cell off the grid, Ganon's
    Tower's 0x4C) take the floor of the tile a cell over, away from the spikes (once the
    room's floors are all laid, drained basins too)."""
    if dungeon in KEEP_AS_IS:
        return
    attr = plan.room.attr
    kinds = SPIKE_FLOOR_ROOMS.get(plan.id, (SPIKE_FLOOR,))
    walk = [c in PASSABLE for c in plan.cells]
    for t in range(4096):
        tx, ty = t % 64, t // 64
        if attr[t] not in kinds or not walk[(ty // 2) * 32 + tx // 2]:
            continue
        for nx, ny in ((tx + 2, ty), (tx - 2, ty), (tx, ty + 2), (tx, ty - 2)):
            if 0 <= nx < 64 and 0 <= ny < 64 and attr[ny * 64 + nx] not in kinds and \
                    walk[(ny // 2) * 32 + nx // 2] and not empty[ny * 8:ny * 8 + 8, nx * 8:nx * 8 + 8].any():
                for a in (img, empty, stoned):
                    a[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8] = a[ny * 8:ny * 8 + 8, nx * 8:nx * 8 + 8]
                break


# rooms whose floor A Link to the Past lights with beams of color math (Agahnim's hall in
# Ganon's Tower, light from its windows falling across the red tiles): without the
# blending the beams are patches of another floor, cut in stair-steps. The floor is laid
# as one, in its own color
# (room: the characters and palettes of its floor tiles, beams or not)
ONE_FLOOR_ROOMS = {0x0d: (set(range(0xc7, 0xcc)) | set(range(0xd8, 0xdc)), (5, 6))}


def one_floor(plan, gfx, img, empty, stoned, slab_base):
    if plan.id not in ONE_FLOOR_ROOMS:
        return
    chars, pals = ONE_FLOOR_ROOMS[plan.id]
    walk = [c in PASSABLE for c in plan.cells]
    tiles = [t for t in range(4096) if walk[(t // 64 // 2) * 32 + t % 64 // 2] and
             (gfx.bg2[t] & 0x3ff) in chars and (gfx.bg2[t] >> 10) & 7 in pals]
    lay = stoned & (slab_base.sum(axis=-1) > 0)
    if not tiles or not lay.any():
        return
    cols, counts = np.unique(slab_base[lay].reshape(-1, 3), axis=0, return_counts=True)
    base = cols[counts.argmax()]
    pat = stone(np.array(base, dtype=float))
    for t in tiles:
        s = (slice(t // 64 * 8, t // 64 * 8 + 8), slice(t % 64 * 8, t % 64 * 8 + 8))
        img[s] = pat[s]
        empty[s] = False
        stoned[s] = True
        slab_base[s] = base


# stray marks left on a floor Link walks: cracks A Link to the Past breaks open with a bomb
# (attribute 0x62) that lead nowhere in the blend (the Ice Palace's 0x7E drops Link through
# its hole instead), and the dotted rims of the rocky ledges in Turtle Rock's 0x15, which
# the blend's one floor lays flat (characters of the room's graphics)
STRAY_ATTRS = {0x7e: (0x62,)}
STRAY_CHARS = {0x15: (0x0ac, 0x0ad, 0x0ae, 0x0af, 0x070, 0x071)}


def cracks(plan, gfx, img, empty, stoned, slab_base):
    attrs = STRAY_ATTRS.get(plan.id, ())
    chars = STRAY_CHARS.get(plan.id, ())
    if not attrs and not chars:
        return
    shown = front_chars(gfx) if chars else None
    stray = lambda t: plan.room.attr[t] in attrs or (shown is not None and shown[t] in chars)
    walk = [c in PASSABLE for c in plan.cells]
    todo = [t for t in range(4096) if stray(t) and walk[(t // 64 // 2) * 32 + t % 64 // 2]]
    for t in todo:
        tx, ty = t % 64, t // 64
        around = [tuple(slab_base[ny * 8 + 4, nx * 8 + 4]) for ny in range(ty - 2, ty + 3) for nx in range(tx - 2, tx + 3)
                  if 0 <= ny < 64 and 0 <= nx < 64 and not stray(ny * 64 + nx) and
                  stoned[ny * 8:ny * 8 + 8, nx * 8:nx * 8 + 8].all()]
        if not around:
            continue
        base = max(set(around), key=around.count)
        s = (slice(ty * 8, ty * 8 + 8), slice(tx * 8, tx * 8 + 8))
        img[s] = stone(np.array(base, dtype=float))[s]
        empty[s] = False
        stoned[s] = True
        slab_base[s] = base


# cells whose own art stays though the plan redraws them as floor: Misery Mire's 0xC2 has a
# block in a channel of its lower layer, under its upper floor, where its wall runs across
# (redrawn, it cut a notch in the wall)
KEEP_ART = {0xc2: {(13, 7), (13, 8)}}


# the art of a staircase walled up (build.Plan.seal_stairs) becomes the wall around it:
# room: (x0, y0, x1, y1) 8px tiles of the staircase, exclusive, and the column of wall
# beside it that is laid across it
SEALED_STAIR_ART = {0x16: ((46, 4, 50, 7), 45)}


def sealed_stair_art(plan, img, empty):
    if plan.id not in SEALED_STAIR_ART or not getattr(plan, 'sealed_cells', None):
        return
    (x0, y0, x1, y1), col = SEALED_STAIR_ART[plan.id]
    for ty in range(y0, y1):
        for tx in range(x0, x1):
            for a in (img, empty):
                a[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8] = a[ty * 8:ty * 8 + 8, col * 8:col * 8 + 8]


# rooms whose pegs stay flat and are shown lowered, flush squares in the floor, rather than
# laid over as the floor (tileset.Blend.paint): Turtle Rock's 0xC4, whose blocks joined by
# pegs read as fences with their gates down
FLAT_PEG_LOOK = set()  # Turtle Rock 0xC4 read too busy with lowered pegs; its pegs stay plain floor
