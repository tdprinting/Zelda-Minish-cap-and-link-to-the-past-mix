#!/usr/bin/env python3
"""
Builds the blended overworld from your own Minish Cap build (tmc/build/USA/assets):

  * every overworld palette recolored toward A Link to the Past's Light World and
    pushed into the cel-shaded look (owstyle.py);
  * soft shadows for the overworld rooms, drawn on a blended background layer at
    run time (shadows.py, tmc/src/blendShadows.c).

    python3 blend/overworld/build.py [--alttp path/to/alttp-usa.sfc]

The A Link to the Past ROM supplies the Light World colors the palettes move
toward; they are cached in tmc/custom/gen/ow, so later runs can leave it out.
Everything written goes to tmc/custom/gen/ow, which stays out of git: it holds
data from your ROMs.
  assets/palettes/gPalette_<n>.gbapal   restyled palettes; CUSTOM builds look in
                                         custom/gen/ow/assets before the build's own
  shadows.inc, shadow_*.bin             the shadow layer (included by data/map/map_data.s)
"""
import argparse
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import owdata  # noqa: E402
import owstyle  # noqa: E402
import restyle  # noqa: E402
import shadows  # noqa: E402
import alttpow  # noqa: E402
import exteriors  # noqa: E402

OUT = os.path.join(owdata.TMC, 'custom', 'gen', 'ow')
REFERENCE = os.path.join(OUT, 'alttp_light_world.rgb')

# (area index, name, shadows?) - shadows where the game leaves background 3 free,
# plus Hyrule Field, whose drifting cloud cover gives way to them.
# Hyrule Town and the Minish Village stream their tiles while you walk, and the
# Royal Valley keeps its fog, so those keep only the new colors.
AREAS = [
    (0x00, 'Minish Woods', True),
    (0x01, 'Minish Village', False),
    (0x02, 'Hyrule Town', False),
    (0x03, 'Hyrule Field', True),
    (0x04, 'Castor Wilds', True),
    (0x05, 'Wind Ruins', True),
    (0x06, 'Mt. Crenel', True),
    (0x07, 'Hyrule Castle Garden', True),
    (0x08, 'Cloud Tops', True),
    (0x09, 'Royal Valley', False),
    (0x0A, 'Veil Falls', True),
    (0x0B, 'Lake Hylia', True),
    (0x15, 'Festival Town', False),
    (0x1A, 'Veil Falls summit', True),
]

MAX_CHARS = 63          # a cell byte holds the character number in 6 bits


def ground_color(plan, ts):
    counts = plan.ground_colors(ts)
    if not counts:
        return (120, 160, 96)
    v = max(counts, key=counts.get)
    return owdata.gba_to_rgb(v)


def shadow_slots(plan, ts):
    """A palette row with unused slots for the shadow colors (3 to 5 of them)."""
    free = plan.free_palette_slots(ts)
    for row in sorted(free, key=lambda r: -len(free[r])):
        if len(free[row]) >= 3:
            return row, free[row][:shadows.MAX_SHADES]
    return None, None


def encode_char(levels, idx):
    px = np.zeros((8, 8), dtype=np.uint8)
    for lvl in range(1, len(idx) + 1):
        px[levels == lvl] = idx[lvl - 1]
    return owdata.encode_chars(px)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--alttp', help='A Link to the Past (USA) ROM, for its Light World colors')
    ap.add_argument('--quiet', action='store_true')
    args = ap.parse_args()
    log = (lambda *a: None) if args.quiet else print
    os.makedirs(OUT, exist_ok=True)

    if args.alttp:
        owstyle.save_reference(owstyle.alttp_light_world(args.alttp), REFERENCE)
    if os.path.exists(REFERENCE):
        reference = owstyle.load_reference(REFERENCE)
    else:
        log('No A Link to the Past colors yet (pass --alttp once): palettes keep Minish Cap hues.')
        reference = []
    style = owstyle.Style(reference)

    pal_dir = os.path.join(OUT, 'assets', 'palettes')
    os.makedirs(pal_dir, exist_ok=True)
    sets = []           # (row, slots, gfx file)
    rooms = []          # (area, room, set, w, h, cells file)
    written = set()
    fronts = []         # dungeon fronts and landmarks (exteriors.py)
    have_alttp = alttpow.available()
    if not have_alttp:
        log('No A Link to the Past overworld dump yet (make-dungeons.sh makes it): no dungeon fronts.')
    for index, name, want_shadows in AREAS:
        plan = restyle.AreaPlan(index, style, log=log)
        plan.run()
        log('%s (area 0x%02x): %d tileset(s)' % (name, index, len(plan.tilesets)))
        if have_alttp:
            palettes = {}
            for ts in plan.tilesets:
                for row, cols in ts.rows.items():
                    palettes[ts.pids[row]] = cols
            pool, meta_pool, prior = [], [], {}
            here = exteriors.merge([exteriors.build_site(site, palettes, pool, meta_pool, log=log, prior=prior)
                                    for site in exteriors.SITES if site.area == index])
            if here:
                exteriors.patch_plan(plan, here)
            fronts += here
        if want_shadows:
            make_shadows(plan, sets, rooms, log)
        for ts in plan.tilesets:
            for row, cols in ts.rows.items():
                pid = ts.pids[row]
                if pid in written:
                    continue
                written.add(pid)
                with open(os.path.join(pal_dir, 'gPalette_%d.gbapal' % pid), 'wb') as f:
                    f.write(struct.pack('<16H', *cols))
    write_inc(sets, rooms)
    exteriors.write(fronts, OUT, log=log)
    log('Wrote %d palettes, %d shadow sets, %d rooms with shadows to %s'
        % (len(written), len(sets), len(rooms), OUT))


def make_shadows(plan, sets, rooms, log):
    by_ts = {}
    for ts in plan.tilesets:
        if not ts.rooms:
            continue
        free = plan.free_top_chars(ts)
        row, idx = shadow_slots(plan, ts)
        if row is None or len(free) < 8:
            log('  tileset %d: no room for shadows (free characters %d, palette row %s)' % (ts.ts_index, len(free), row))
            continue
        by_ts[ts.ts_index] = (free, row, idx)
    if not by_ts:
        return
    budget = min(MAX_CHARS, min(len(v[0]) for v in by_ts.values()) - 1)
    area = shadows.ShadowArea(plan, budget=budget, log=log)
    for tsi, (free, row, idx) in by_ts.items():
        ts = plan.tilesets[tsi]
        chars = area.chars(len(idx))
        colors = shadows.shadow_colors(ground_color(plan, ts), len(idx))
        for k, c in zip(idx, colors):
            ts.rows[row][k] = owdata.rgb_to_gba(c)
        slots = free[:len(chars) + 1]
        gfx = bytes(32)            # character 0: blank
        for n in range(1, len(chars) + 1):
            gfx += encode_char(chars[n], idx)
        set_no = len(sets)
        name = 'shadow_set_%d.bin' % set_no
        open(os.path.join(OUT, name), 'wb').write(gfx)
        sets.append((row, [s - 1024 for s in slots], name, plan.label, tsi))
        log('  tileset %d: %d shadow characters, colors in row %d slots %s: %s'
            % (tsi, len(chars), row, idx, colors))
        for key, (rtsi, w, h, C) in sorted(area.rooms.items(), key=lambda kv: str(kv[0])):
            if rtsi != tsi:
                continue
            # a room with a dungeon front is shaded once per world (exteriors.patch_plan)
            room, world = key if isinstance(key, tuple) else (key, 0)
            cm = area.cell_map(key)
            cells = (cm[..., 0] | (cm[..., 1] << 6) | (cm[..., 2] << 7)).astype(np.uint8)
            fname = 'shadow_%02x_%02x%s.bin' % (plan.index, room, '_w%d' % world if world else '')
            open(os.path.join(OUT, fname), 'wb').write(cells.tobytes())
            rooms.append((plan.index, room, set_no, w * 2, h * 2, fname, world))


def write_inc(sets, rooms):
    L = ['@ Soft overworld shadows, made by blend/overworld/build.py from your own build. Do not edit.',
         '\t.align 2', 'gBlendShadowSets::']
    for n, (row, slots, name, label, tsi) in enumerate(sets):
        L.append('\t@ %s, tileset %d' % (label, tsi))
        L.append('\t.byte %d, %d, 0, 0' % (row, len(slots)))
        L.append('\t.4byte gBlendShadowSlots_%d, gBlendShadowGfx_%d' % (n, n))
    L += ['\t.align 2', 'gBlendShadowRooms::']
    for area, room, set_no, w, h, fname, world in rooms:
        L.append('\t.byte 0x%02x, 0x%02x, %d, %d' % (area, room, set_no, world))
        L.append('\t.2byte %d, %d' % (w, h))
        L.append('\t.4byte gBlendShadowCells_%s' % fname[7:-4])
    L.append('\t.byte 0xff, 0xff, 0, 0')
    L.append('\t.2byte 0, 0')
    L.append('\t.4byte 0')
    for n, (row, slots, name, label, tsi) in enumerate(sets):
        L += ['\t.align 2', 'gBlendShadowSlots_%d::' % n, '\t.2byte ' + ', '.join(str(s) for s in slots)]
        L += ['\t.align 2', 'gBlendShadowGfx_%d::' % n, '\t.incbin "custom/gen/ow/%s"' % name]
    for area, room, set_no, w, h, fname, world in rooms:
        L += ['gBlendShadowCells_%s::' % fname[7:-4], '\t.incbin "custom/gen/ow/%s"' % fname]
    L.append('\t.align 2')
    open(os.path.join(OUT, 'shadows.inc'), 'w').write('\n'.join(L) + '\n')


if __name__ == '__main__':
    main()
