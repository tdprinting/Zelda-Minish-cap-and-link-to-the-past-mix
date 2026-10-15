"""
Co-op locks: in two-player co-op every dungeon plays differently. Doors on the way to
the boss stay shut until the two players solve a puzzle together (blendCoopDungeons.c
spawns the doors and plates, only in co-op; alone, the dungeon is as it was).

This picks, for each A Link to the Past dungeon the blend builds, two doors on the main
way (the entrance to the boss), and the plates that open them:
- the first: twin plates far apart that must be down at once, or a great plate (2x2)
  that takes both players' weight;
- the second: a hold-and-cross plate. One player holds a plate by the door, which stays
  open only while it is down; the other crosses and steps on a plate in the next room,
  which opens the door for good.
"""
from collections import deque

import alttprooms as A
import tileset

DIR_N, DIR_S, DIR_W, DIR_E = 0, 1, 2, 3
MC_DIR = {DIR_N: 0, DIR_E: 1, DIR_S: 2, DIR_W: 3}
OPPOSITE = {DIR_N: DIR_S, DIR_S: DIR_N, DIR_W: DIR_E, DIR_E: DIR_W}
INWARD = {DIR_N: (0, 1), DIR_S: (0, -1), DIR_W: (1, 0), DIR_E: (-1, 0)}

TWIN, GREAT, RELAY = 0, 1, 2
KIND_NAMES = {TWIN: 'COOP_TWIN', GREAT: 'COOP_GREAT', RELAY: 'COOP_RELAY'}


def _walkable(plan, x, y, blocked):
    return (x, y) not in blocked and plan.at(x, y) in (A.FLOOR, A.DOOR)


def reach(plan, starts, blocked):
    """Cells Link can walk to from `starts` without passing `blocked`."""
    seen = set()
    q = deque(s for s in starts if _walkable(plan, s[0], s[1], blocked))
    while q:
        p = q.popleft()
        if p in seen:
            continue
        seen.add(p)
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (p[0] + dx, p[1] + dy)
            if n not in seen and _walkable(plan, n[0], n[1], blocked):
                q.append(n)
    return seen


def _taken(plan):
    """Cells a plate may not cover: chests, stairs, holes, steps, door mouths and pegs."""
    t = {(c[0], c[1]) for c in plan.chests} | {(s[0], s[1]) for s in plan.stairs}
    t |= {(d[0], d[1]) for d in plan.drops} | set(plan.steps_up)
    for d in plan.doors:
        x, y = d['inner']
        t |= {(x + dx, y + dy) for dx in range(-1, 2) for dy in range(-1, 2)}
    attr = plan.room.attr
    for i in range(4096):
        if attr[i] in tileset.PEG_ATTRS:
            t.add(((i % 64) // 2, (i // 64) // 2))
    return t


def _open(plan, p, cells):
    """A plate cell out in the open: floor all around it, so it blocks no way."""
    return all((p[0] + dx, p[1] + dy) in cells and plan.at(p[0] + dx, p[1] + dy) == A.FLOOR
               for dx in (-1, 0, 1) for dy in (-1, 0, 1))


def _dist(a, b):
    return abs(a[0] - b[0]) + abs(a[1] - b[1])


def graph(dg, plans, edges):
    """Each room's ways on: [(next room, how, cell in this room, cell Link lands on there)]."""
    out = {r: [] for r in plans}
    for r, p in plans.items():
        for d in p.doors:
            o = d['other']
            if d['kind'] == 'exit' or o == r or o not in plans:
                continue
            back = [e for e in plans[o].doors if e['other'] == r and e['dir'] == OPPOSITE[d['dir']]]
            if back:
                out[r].append((o, d, d['inner'], back[0]['inner']))
        for (x, y) in edges[r]:
            side = DIR_N if y == 0 else DIR_S if y == 31 else DIR_W if x == 0 else DIR_E
            o = r + {DIR_N: -16, DIR_S: 16, DIR_W: -1, DIR_E: 1}[side]
            if o in plans:
                land = (x, 31 - y) if side in (DIR_N, DIR_S) else (31 - x, y)
                out[r].append((o, 'edge', (x, y), land))
        for (cx, cy, j) in p.stairs:
            o = p.room.travel[j + 1] if j + 1 < len(p.room.travel) else 0
            if o in plans and o != r:
                land = _stairs_land(plans, r, p, o, cx, cy)
                if land:
                    out[r].append((o, 'stairs', (cx, cy), land))
        for (cx, cy, o, land) in p.drops:
            if o in plans and land:
                out[r].append((o, 'drop', (cx, cy), tuple(land)))
        link = dg.get('links', {}).get(r)
        if link is not None and link in plans and link_cell:
            x, y = arrival(plans[link])
            out[r].append((link, 'link', link_cell(p), ((x - 8) // 16, (y - 8) // 16)))
    return out


def _stairs_land(plans, r, p, o, cx, cy):
    """Where stairs take Link, as build.py's exits work it out."""
    q = plans[o]
    mine = sorted((y, x) for (x, y, k) in p.stairs if p.room.travel[k + 1] == o)
    theirs = sorted((dy, dx) for (dx, dy, dj) in q.stairs if q.room.travel[dj + 1] == r)
    land = None
    if theirs:
        n = mine.index((cy, cx))
        land = theirs[min(n, len(theirs) - 1)][::-1]
    if land is None and q.stairs:
        land = q.stairs[0][:2]
    if land is None:
        return None
    lx, ly = land
    for ox, oy in ((0, 1), (0, -1), (1, 0), (-1, 0), (0, 2)):
        if q.at(lx + ox, ly + oy) == A.FLOOR:
            return (lx + ox, ly + oy)
    return land


# set by build.py (its helpers for the links between rooms by way of the outside)
link_cell = arrival = None


def main_way(dg, plans, edges):
    """The rooms from the entrance to the boss, each with the way Link came in by."""
    target = None
    for r in dg['rooms']:
        for d in plans[r].doors:
            if d['kind'] == 'boss':
                target = d['other'] if d['other'] in plans else r
    if target is None:
        target = dg['rooms'][-1]
    g = graph(dg, plans, edges)
    start = dg['entrance']
    entry = [d['inner'] for d in plans[start].doors if d['kind'] == 'exit'] or [plans[start].middle()]
    prev = {start: (None, None, entry[0])}
    q = deque([start])
    while q:
        r = q.popleft()
        for (o, how, here, land) in g[r]:
            if o not in prev:
                prev[o] = (r, how, land)
                q.append(o)
    if target not in prev:
        # (a boss Link reaches some way the plan doesn't follow: as far as it goes)
        depth = {}
        for r in prev:
            n, q2 = 0, r
            while prev[q2][0] is not None:
                q2, n = prev[q2][0], n + 1
            depth[r] = n
        target = max(prev, key=lambda r: (depth[r], -dg['rooms'].index(r)))
    main_way.prev = prev
    way = []
    r = target
    while r is not None:
        way.append((r,) + prev[r])
        r = prev[r][0]
    way.reverse()
    # [(room, came from, how, cell Link enters at)]
    return way


def _twin(plan, cells, door):
    spots = [p for p in cells if _open(plan, p, cells)]
    best = None
    for i, a in enumerate(spots):
        for b in spots[i + 1:]:
            d = _dist(a, b)
            # far enough apart that one player can't stand on both, near the door
            if d < 9 or abs(a[0] - b[0]) + abs(a[1] - b[1]) > 18:
                continue
            score = _dist(a, door) + _dist(b, door) - min(d, 14)
            if best is None or score < best[0]:
                best = (score, [a, b])
    return best and best[1]


def _great(plan, cells, door):
    spots = []
    for (x, y) in cells:
        sq = [(x, y), (x + 1, y), (x, y + 1), (x + 1, y + 1)]
        if all(s in cells and _open(plan, s, cells) for s in sq) and _dist((x, y), door) >= 3:
            spots.append(sq)
    if not spots:
        return None
    return min(spots, key=lambda sq: _dist(sq[0], door))


def _near(plan, cells, door, dmin, dmax, off_line=True):
    dx, dy = INWARD[door[2]]
    spots = [p for p in cells if _open(plan, p, cells) and dmin <= _dist(p, door[:2]) <= dmax]
    if off_line:
        # beside the way through, not in it
        spots = [p for p in spots if (p[0] != door[0] if dx == 0 else p[1] != door[1])]
    if not spots:
        return None
    return min(spots, key=lambda p: (_dist(p, door[:2]), p))


def choose(dg, plans, edges, number):
    """The dungeon's co-op locks: [(kind, room, gate dir, gate cell, [(room, cell)] plates)].
    A gate stands in a doorway, or over stairs or a step on the way."""
    way = main_way(dg, plans, edges)
    steps = []
    for i in range(len(way) - 1):
        r, nxt = way[i][0], way[i + 1]
        how = nxt[2]
        if isinstance(how, dict):
            if how['kind'] != 'open' or how.get('edge') is None:
                continue
            steps.append((i, r, way[i][3], how['inner'], how['dir'], nxt[0], nxt[3]))
        elif how in ('stairs', 'link'):
            cell = [h for (o, k, h, land) in graph(dg, plans, edges)[r] if o == nxt[0] and k == how][0]
            steps.append((i, r, way[i][3], cell, DIR_N, nxt[0], nxt[3]))
    # side ways too, the farthest first, should the main way have too few doors
    prev = main_way.prev
    on_way = {w[0] for w in way}
    side = []
    for o, (r, how, land) in prev.items():
        if r is None or o in on_way and r in on_way or not isinstance(how, dict):
            continue
        if how['kind'] == 'open' and how.get('edge') is not None:
            came = prev[r][2]
            side.append((len(way) + len(side), r, came, how['inner'], how['dir'], o, land))
    locks = []
    used = set()

    def place(kind, step):
        i, r, came, door, dr, o, land = step
        p = plans[r]
        taken = _taken(p) | {door}
        cells = reach(p, [came], {door})
        if door in cells or not cells:
            return None
        free = {c for c in cells if c not in taken}
        if kind == TWIN:
            plates = _twin(p, free, door)
            plates = plates and [(r, c) for c in plates]
        elif kind == GREAT:
            plates = _great(p, free, door)
            plates = plates and [(r, c) for c in plates]
        else:
            a = _near(p, free, door + (dr,), 2, 7) or _near(p, free, door + (dr,), 2, 9, off_line=False)
            q = plans[o]
            qcells = reach(q, [land], set())
            qfree = {c for c in qcells if c not in _taken(q)}
            b = _near(q, qfree, land + (OPPOSITE[dr],), 2, 9, off_line=False)
            plates = a and b and [(r, a), (o, b)]
        if not plates:
            return None
        return (kind, r, dr, door, plates)

    first = TWIN if number % 2 == 0 else GREAT
    # doorways first: a gate over stairs only where the way has no door
    order = sorted(steps, key=lambda s: (s[4] == DIR_N and s[3] not in [d['inner'] for d in plans[s[1]].doors], s[0]))
    for step in order:
        lock = place(first, step) or place(GREAT if first == TWIN else TWIN, step)
        if lock:
            locks.append(lock)
            used.add(step[1])
            break
    # the second lock: past halfway along the way, in another room
    later = [s for s in steps if s[1] not in used and s[0] >= len(way) // 2] + \
            [s for s in steps if s[1] not in used and s[0] < len(way) // 2][::-1]
    for step in later + side:
        if step[1] in used:
            continue
        lock = place(RELAY, step)
        if lock:
            locks.append(lock)
            used.add(step[1])
            break
    if not locks:
        for step in side:
            lock = place(first, step)
            if lock:
                locks.append(lock)
                break
    return locks


def header(entries):
    """coop.h for blendCoopDungeons.c: the locks of the A Link to the Past dungeons."""
    lines = ['// the co-op locks of the A Link to the Past dungeons (blend/dungeons/coop.py)',
             '#define COOP_GEN_LOCKS \\']
    for (area, index, kind, dr, door, plates) in entries:
        cells = ', '.join('{ %d, 0x%x }' % (pr, c[1] * 64 + c[0]) for (pr, c) in plates)
        lines.append('    { %s, %d, %s, %d, 0x%x, %d, { %s } }, \\' % (
            area, index, KIND_NAMES[kind], MC_DIR[dr], door[1] * 64 + door[0], len(plates), cells))
    lines.append('')
    return lines
