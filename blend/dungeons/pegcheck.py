"""
Whether a room's crystal switch pegs can stand up without shutting Link out of
anything: tileset.Blend draws pegs that rise and fall only in rooms that pass.

With the pegs flat (as the blend drew them before its switches worked), Link reaches
a set of the room's doors, stairs, holes and chests from each way in. With them
working, he must still reach all of those from each way in, whichever color is up
when he comes in, hitting the room's own switches (from beside them) as often as he
likes. A room where some color of pegs would cut him off keeps its pegs flat.
"""
import tileset

PASSABLE = tileset.PASSABLE


def _nbrs(i):
    x, y = i % 32, i // 32
    for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
        if 0 <= nx < 32 and 0 <= ny < 32:
            yield ny * 32 + nx


def places(plan, edges=()):
    """The room's ways in and out (cells) and its chests (cells)."""
    ways = {d['inner'][1] * 32 + d['inner'][0] for d in plan.doors}
    ways |= {y * 32 + x for x, y in edges}
    ways |= {y * 32 + x for x, y, _ in plan.stairs}
    ways |= {y * 32 + x for x, y, _, _ in plan.drops}
    ways |= {y * 32 + x for x, y in plan.steps_up}
    chests = {y * 32 + x for x, y, _ in plan.chests}
    return ways - chests, chests


def check(plan, pegs, edges=()):
    """None if the room's pegs can work, else what they would cut off (text)."""
    ways, chests = places(plan, edges)
    raised = [tileset.Blend.raised_cells(pegs, s) for s in (0, 1)]
    switches = {cy * 32 + cx for cx, cy in pegs['switches']}
    pegged = raised[0] | raised[1]
    base = [plan.cells[i] in PASSABLE and i not in chests and i not in switches for i in range(1024)]

    def reach(start, state, working):
        seen = set()
        stack = [(start, state)]
        while stack:
            c, s = stack.pop()
            if (c, s) in seen:
                continue
            seen.add((c, s))
            for n in _nbrs(c):
                if working and n in switches:
                    stack.append((c, 1 - s))
                elif base[n] and not (working and n in raised[s] - switches):
                    stack.append((n, s))
        cells = {c for c, _ in seen}
        got = cells & ways
        got |= {ch for ch in chests if any(n in cells for n in _nbrs(ch))}
        return got

    problems = []
    for e in sorted(ways):
        if not base[e]:
            continue
        flat = reach(e, 0, False)
        for s in (0, 1):
            if e in raised[s]:
                continue  # (a peg can't rise under Link: he steps off first)
            lost = flat - reach(e, s, True)
            if lost:
                problems.append('from (%d, %d) with color %d up: %s' % (
                    e % 32, e // 32, s, ', '.join('(%d, %d)' % (c % 32, c // 32) for c in sorted(lost))))
    return '; '.join(problems) or None
