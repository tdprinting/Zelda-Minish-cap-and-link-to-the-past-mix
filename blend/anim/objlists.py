"""
Splices new frame object lists into Minish Cap's gFrameObjLists.

Each generator writes tmc/custom/gen/objlists/<sprite>.s, which defines
BlendObjLists_<sprite>: the sprite's per-frame offsets (written as
`Label - gFrameObjLists`) followed by its object lists. This copies the game's
table to tmc/custom/gen/frame_obj_lists.inc with the first-level entry of each
such sprite pointed at its new lists, and includes them after the table.

    python3 blend/anim/objlists.py      (after the generators, before make)
"""
import glob
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
TMC = os.path.join(ROOT, 'tmc')
SRC = os.path.join(TMC, 'build', 'USA', 'assets', 'gfx', 'gFrameObjLists.s')
GEN = os.path.join(TMC, 'custom', 'gen')


def main():
    lines = open(SRC).read().split('\n')
    patches = {}
    for path in sorted(glob.glob(os.path.join(GEN, 'objlists', '*.s'))):
        name = os.path.basename(path)[:-2]
        if name.isdigit():
            patches[int(name)] = os.path.relpath(path, TMC)
    entry = -1
    for i, line in enumerate(lines):
        if line.startswith('@ Second level'):
            break
        if line.strip().startswith('.4byte'):
            entry += 1
            if entry in patches:
                lines[i] = '\t.4byte BlendObjLists_%d - gFrameObjLists @ was %s' % (entry, line.split()[-1])
    missing = [n for n in patches if n > entry]
    if missing:
        raise SystemExit('gFrameObjLists has no entry for sprite(s) %s' % missing)
    out = ['@ Made by blend/anim/objlists.py from the game\'s gFrameObjLists.'] + lines
    out.append('\t.byte 0xff, 0xff, 0xff')
    for n in sorted(patches):
        out.append('\t.include "%s"' % patches[n])
    os.makedirs(GEN, exist_ok=True)
    with open(os.path.join(GEN, 'frame_obj_lists.inc'), 'w') as f:
        f.write('\n'.join(out) + '\n')
    print('gFrameObjLists: new lists for sprite(s) %s' % ', '.join(map(str, sorted(patches))))


if __name__ == '__main__':
    main()
