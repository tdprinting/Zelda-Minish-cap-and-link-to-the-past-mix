"""
Reads one Minish Cap overworld area from the tmc build: its tile graphics
(characters), metatiles, tile types, palettes and room maps, and draws rooms
with numpy, keeping for every pixel which character, palette row and color
index it came from. Everything comes from the user's own build directory.
"""
import os
import re
import struct

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
TMC = os.path.normpath(os.path.join(HERE, '..', '..', 'tmc'))
ASSETS = os.path.join(TMC, 'build', 'USA', 'assets')
AREAS = os.path.join(ASSETS, 'maps', 'areas')

FILLER_TYPE = 948      # tile type of the unused metatile slots in overworld sets
TOP_BASE = 512         # the top layer's characters start at 0x6004000


def read_u16(path):
    d = open(path, 'rb').read()
    return np.frombuffer(d, dtype='<u2').astype(np.int64)


def asm(path):
    return open(os.path.join(TMC, path)).read()


def gba_to_rgb(c):
    return ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)


def rgb_to_gba(rgb):
    r, g, b = [min(31, max(0, int(round(v / 8.0)))) for v in rgb]
    return r | (g << 5) | (b << 10)


def decode_chars(data):
    """bytes of 4bpp characters -> (n, 8, 8) array of color indices."""
    a = np.frombuffer(data, dtype=np.uint8)
    lo = a & 15
    hi = a >> 4
    px = np.stack([lo, hi], axis=-1).reshape(-1, 8, 8)
    return px.astype(np.uint8)


def encode_chars(px):
    px = px.reshape(-1, 32, 2).astype(np.uint8)
    return (px[:, :, 0] | (px[:, :, 1] << 4)).astype(np.uint8).tobytes()


# ---------------------------------------------------------------- asm tables

def _symbol_files():
    """symbol -> asset path, from data/map/map_data.s and data/gfx/gfx_and_palettes.s"""
    out = {}
    for f in ('data/map/map_data.s', 'data/gfx/gfx_and_palettes.s'):
        cur = None
        for line in asm(f).split('\n'):
            m = re.match(r'^(\w+)::', line)
            if m:
                cur = m.group(1)
                continue
            m = re.match(r'\s*\.incbin "([^"]+)"', line)
            if m and cur:
                out[cur] = m.group(1)
                cur = None
    return out


SYMBOLS = _symbol_files()


def asset_path(sym):
    return os.path.join(ASSETS, SYMBOLS[sym])


def raw(path):
    """An asset's uncompressed bytes: the build keeps them next to the .lz."""
    if path.endswith('.lz'):
        path = path[:-3]
    return open(path, 'rb').read()


def area_label_tables():
    """area index -> (tileset label, tiles label) from the gAreaTileSets / gAreaTiles tables."""
    def table(src, name):
        start = src.index(name + ':: ')
        out = []
        for line in src[start:].split('\n')[1:]:
            m = re.match(r'\s*\.4byte (\w+)', line)
            if not m:
                break
            out.append(m.group(1))
        return out
    ts = table(asm('data/map/tileset_headers.s'), 'gAreaTileSets')
    tl = table(asm('data/map/tile_headers.s'), 'gAreaTiles')
    return ts, tl


def label_block(src, label):
    start = src.index(label + '::')
    lines = []
    for line in src[start:].split('\n')[1:]:
        if re.match(r'^\w+::', line) or (not line.strip() and lines):
            break
        if line.strip():
            lines.append(line.strip())
    return lines


def tileset_list(label):
    """gAreaTileSets_X -> [gAreaTileSet_X_0, ...]"""
    return [l.split()[1] for l in label_block(asm('data/map/tileset_headers.s'), label) if l.startswith('.4byte')]


def tileset_parts(label, src_file='data/map/tileset_headers.s'):
    """A tileset header -> ([(symbol, dest, size)], palette group)."""
    parts, group = [], None
    for l in label_block(asm(src_file), label):
        if l.startswith('tileSet_tiles'):
            a = [x.strip() for x in l.split(None, 1)[1].split(',')]
            parts.append((a[0].replace('offset_', ''), int(a[1], 0), int(a[2], 0)))
        elif l.startswith('tileSet_palette_set'):
            group = int(l.split(None, 1)[1].split(',')[0], 0)
    return parts, group


def tiles_parts(label):
    """gAreaTiles_X -> {'bottom': sym, 'top': sym, 'types_bottom': sym, 'types_top': sym}"""
    out = {}
    for l in label_block(asm('data/map/tile_headers.s'), label):
        kind = l.split()[0]
        sym = l.split(None, 1)[1].split(',')[0].strip().replace('offset_', '')
        key = {'tiles_bottom': 'bottom', 'tiles_top': 'top', 'tile_types_bottom': 'types_bottom',
               'tile_types_top': 'types_top'}.get(kind)
        if key:
            out[key] = sym
    return out


def palette_group(group):
    """[(first palette id, dest row, count)] for gPaletteGroup_<group>."""
    src = asm('data/gfx/palette_groups.s')
    out = []
    for l in label_block(src, 'gPaletteGroup_%d' % group):
        m = re.match(r'palette_set palette=pal_(\d+), offset=(\w+), count=(\w+)', l)
        if m:
            out.append((int(m.group(1)), int(m.group(2), 0), int(m.group(3), 0)))
    return out


def palette_rows(group):
    """{row: (palette id, [16 gba colors])} for a palette group."""
    rows = {}
    for pid, dest, count in palette_group(group):
        for i in range(count or 16):
            data = open(asset_path('gPalette_%d' % (pid + i)), 'rb').read()
            rows[dest + i] = (pid + i, list(struct.unpack('<16H', data[:32])))
    return rows


def room_headers(area_label):
    src = asm('data/map/room_headers.s')
    start = src.index('gAreaRoomHeaders_%s::' % area_label)
    out = []
    for line in src[start:].split('\n')[1:]:
        line = line.strip()
        if not line.startswith('room_header'):
            break
        x, y, w, h, ts = [int(v, 0) for v in line.split(None, 1)[1].split(',')]
        out.append((w // 16, h // 16, ts))
    return out


def room_maps_of(maps_label):
    """[(room index, label, symbol bottom, symbol top)] from a gAreaRoomMaps_ table."""
    src = asm('data/map/map_headers.s')
    out = []
    for i, lab in enumerate(tileset_list_from(src, maps_label)):
        bottom = top = None
        try:
            block = label_block(src, lab)
        except ValueError:
            block = []
        for l in block:
            if l.startswith('map_bottom'):
                bottom = l.split()[1].rstrip(',').replace('offset_', '')
            if l.startswith('map_top'):
                top = l.split()[1].rstrip(',').replace('offset_', '')
        out.append((i, lab, bottom, top))
    return out


def tileset_list_from(src, label):
    return [l.split()[1] for l in label_block(src, label) if l.startswith('.4byte')]


# ---------------------------------------------------------------- the area

def area_tables():
    """Per area index: the labels of its tilesets, metatiles, room maps and room headers."""
    def table(path, name):
        src = asm(path)
        start = src.index(name + ':: ')
        out = []
        for line in src[start:].split('\n')[1:]:
            line = line.strip()
            if not line.startswith('.4byte'):
                break
            out.append(line.split()[1])
        return out
    ts = table('data/map/tileset_headers.s', 'gAreaTileSets')
    tl = table('data/map/tile_headers.s', 'gAreaTiles')
    rm = table('data/map/map_headers.s', 'gAreaRoomMaps')
    rh = table('data/map/room_headers.s', 'gAreaRoomHeaders')
    n = min(len(ts), len(tl), len(rm), len(rh))
    return [dict(tilesets=ts[i], tiles=tl[i], maps=rm[i], headers=rh[i]) for i in range(n)]


class Area:
    """One overworld area (by its index in the area tables) with one of its tilesets loaded."""

    def __init__(self, index, ts_index=0):
        t = area_tables()[index]
        self.index = index
        self.label = t['headers'].replace('gAreaRoomHeaders_', '')
        self.ts_index = ts_index
        self.tileset_label = tileset_list(t['tilesets'])[ts_index]
        self.parts, self.group = tileset_parts(self.tileset_label)
        chars = bytearray(0x10000)
        for sym, dest, size in self.parts:
            data = raw(asset_path(sym))
            o = dest - 0x6000000
            chars[o:o + len(data)] = data
        self.chars = decode_chars(bytes(chars))
        self.rows = palette_rows(self.group)
        tp = tiles_parts(t['tiles'])
        self.tiles_syms = tp
        self.meta = {'bottom': read_u16(raw_path(tp['bottom'])), 'top': read_u16(raw_path(tp['top']))}
        self.types = {'bottom': read_u16(raw_path(tp['types_bottom'])), 'top': read_u16(raw_path(tp['types_top']))}
        self.headers = room_headers(self.label)
        self.maps = room_maps_of(t['maps'])

    def palette(self, extra=None):
        """16x16 RGB palette (rows not in the group from extra, else magenta)."""
        pal = np.zeros((16, 16, 3), dtype=np.int64)
        pal[:] = (248, 0, 248)
        if extra is not None:
            pal[:] = extra
        for r, (pid, cols) in self.rows.items():
            pal[r] = [gba_to_rgb(c) for c in cols]
        return pal

    def rooms(self):
        """Yields (room index, w, h, bottom cells, top cells) for rooms using this tileset."""
        for (i, lab, bsym, tsym), (w, h, ts) in zip(self.maps, self.headers):
            if ts != self.ts_index or bsym is None:
                continue
            b = read_u16(raw_path(bsym))[:w * h]
            t = read_u16(raw_path(tsym))[:w * h]
            yield i, w, h, b, t


def raw_path(sym):
    p = asset_path(sym)
    return p[:-3] if p.endswith('.lz') else p


def draw_layer(area, cells, w, h, layer, chars=None, meta=None):
    """-> (char index, hflip, vflip, row, color index) arrays of (h*16, w*16)."""
    chars = area.chars if chars is None else chars
    meta = area.meta[layer] if meta is None else meta
    base = 0 if layer == 'bottom' else TOP_BASE
    H, W = h * 16, w * 16
    ci = np.zeros((H, W), dtype=np.int64)
    ent = np.zeros((H, W), dtype=np.int64)
    cells = np.asarray(cells).reshape(h, w)
    nmeta = len(meta) // 4
    valid = cells < nmeta
    cl = np.where(valid, cells, 0)
    for k in range(4):
        e = meta[cl * 4 + k]
        e = np.where(valid, e, 0)
        if layer == 'top':
            e = np.where(cells == 0, 0, e)
        oy, ox = (k >> 1) * 8, (k & 1) * 8
        idx = (e & 0x3ff) + base
        hf = (e >> 10) & 1
        vf = (e >> 11) & 1
        for y in range(8):
            for x in range(8):
                sy = np.where(vf == 1, 7 - y, y)
                sx = np.where(hf == 1, 7 - x, x)
                pix = chars[idx, sy, sx]
                ci[oy + y::16, ox + x::16] = pix
                ent[oy + y::16, ox + x::16] = e
    return ent, ci


def to_rgb(pal, ent, ci, transparent=False):
    row = ent >> 12
    rgb = pal[row, ci]
    if transparent:
        return rgb, ci == 0
    return rgb


def render_room(area, b, t, w, h, pal=None, chars=None, meta_b=None, meta_t=None):
    pal = area.palette() if pal is None else pal
    eb, cb = draw_layer(area, b, w, h, 'bottom', chars, meta_b)
    et, ct = draw_layer(area, t, w, h, 'top', chars, meta_t)
    img = pal[eb >> 12, cb].copy()
    img[cb == 0] = pal[0, 0]
    top = ct != 0
    img[top] = pal[et >> 12, ct][top]
    return img.astype(np.uint8)


# ---------------------------------------------------------------- tile animations

def _bg_anim_tables():
    src = asm('src/backgroundAnimations.c')
    gfx = {}
    for m in re.finditer(r'const BgAnimationGfx (\w+)\[\] = \{(.*?)\};', src, re.S):
        ents = re.findall(r'\{\s*(0x[0-9a-fA-F]+|\d+),\s*(\d+),\s*([\w| ]+),\s*offset_(\w+)\s*\}', m.group(2))
        gfx[m.group(1)] = [(int(a, 0), int(b), ('PALETTE' in c), s) for a, b, c, s in ents]
    frames = {}
    for m in re.finditer(r'const BgAnimationFrame (\w+)\[\] = \{(.*?)\};', src, re.S):
        frames[m.group(1)] = [g for g in re.findall(r'\{\s*(\w+),', m.group(2)) if g != 'NULL']
    body = src[src.index('gUnk_080B7278[] = {'):]
    order = re.findall(r'(gUnk_\w+)', body[body.index('{'):body.index('};')])
    lists = {}
    for m in re.finditer(r'const u16 (gUnk_\w+)\[\] = \{([^}]*)\};', src):
        lists[m.group(1)] = [int(v, 0) for v in m.group(2).replace(' ', '').split(',') if v and int(v, 0) != 0xffff]
    body = src[src.index('gUnk_080B755C[] = {'):]
    by_area = [None if v == 'NULL' else v for v in re.findall(r'(gUnk_\w+|NULL)', body[body.index('{'):body.index('};')])]
    return gfx, frames, order, lists, by_area


def area_animations(area_index):
    """[(animation id, [[(vram offset, chars or colors, is palette, gfx symbol)] per frame])]"""
    gfx, frames, order, lists, by_area = _bg_anim_tables()
    lab = by_area[area_index] if area_index < len(by_area) else None
    if lab is None:
        return []
    out = []
    for aid in lists[lab]:
        fr = [gfx[g] for g in frames[order[aid]]]
        out.append((aid, fr))
    return out
