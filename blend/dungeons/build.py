#!/usr/bin/env python3
"""
Rebuilds A Link to the Past dungeons as Minish Cap areas.

Each dungeon keeps A Link to the Past's room plan (walls, pits, doors, stairs,
chests, enemies), read from the user's own cartridge by zelda3's room loader,
and is drawn with a Minish Cap dungeon tileset by the learned autotiler.

Writes, under tmc/custom/gen/ (git-ignored, since it is derived from the ROM):
  <dungeon>_<n>_bottom.bin / _top.bin   room maps
  maps.inc        incbins and map headers, included at the end of map_data.s
  rooms.inc       room headers, included by room_headers.s
  entities.inc    entity and tile entity lists, included by entity_headers.s
  exits.h         room exits, included by transitions.c

Run before `make CUSTOM=1`. Needs the room dump from zelda3 (see README).
"""
import math
import os
import struct

import yaml

import alttprooms as A
import autotile
import calm
import pegcheck
import shrine
import alttpgfx
import tileset

HERE = os.path.dirname(os.path.abspath(__file__))
TMC = os.path.join(HERE, '..', '..', 'tmc')
# (BLEND_OUT: write elsewhere, for a look without touching the game's build)
OUT = os.environ.get('BLEND_OUT') or os.path.join(TMC, 'custom', 'gen')
YAML_DIR = os.environ.get('ALTTP_YAML', '/home/claude/z3/assets/dungeon')

# Minish Cap metatiles in the Fortress of Winds (Castor Darknut) tileset
TILE_CHEST = 16
TILE_STAIRS = 622

# A Link to the Past chest items -> (Minish Cap item, sub value)
ITEMS = {
    0x24: (0x53, 0),     # small key
    0x32: (0x52, 0),     # big key
    0x33: (0x50, 0),     # map
    0x25: (0x51, 0),     # compass
    0x34: (0x54, 0),     # 1 rupee
    0x35: (0x55, 0),     # 5 rupees
    0x36: (0x56, 0),     # 20 rupees
    0x41: (0x57, 0),     # 50 rupees
    0x40: (0x58, 0),     # 100 rupees
    0x28: (0x5d, 0),     # bombs
    0x44: (0x5e, 0),     # arrows
    0x17: (0x63, 0),     # heart piece
    0x0b: (0x09, 0),     # bow
    # Desert Palace's Power Glove: Minish Cap's Power Bracelets do the same lifting
    0x1b: (0x45, 0),
    # Tower of Hera's Moon Pearl already comes with the Ocarina, so the mountain tower
    # holds the Mirror Shield (A Link to the Past has one too; Biggoron's stays)
    0x1f: (0x0e, 0),
    # the Palace of Darkness's Hammer already lies in the dark Eastern Hills, so its big
    # chest holds a bigger quiver (A Link to the Past's quiver upgrade)
    0x09: (0x66, 0),
    # and the Swamp Palace's Hookshot lies in the dark Castor Wilds: a bigger bomb bag
    0x0a: (0x65, 0),
    # Skull Woods's Fire Rod already lies in the dark Minish Woods (and opens the way
    # in): its big chest holds a bigger wallet
    0x07: (0x64, 0),
    # Thieves' Town's Titan's Mitt has no Minish Cap match (the Power Bracelets already do
    # the lifting): its big chest holds the Magic Boomerang, which A Link to the Past has too
    0x1c: (0x0c, 0),
    # the Ice Palace's Blue Mail neither (Minish Cap has no armor): a bigger bomb bag
    0x22: (0x65, 0),
    # Misery Mire's Cane of Somaria already lies at the dark Veil Falls: a bigger wallet
    0x15: (0x64, 0),
    # Turtle Rock's Mirror Shield is the Tower of Hera's already: a bigger quiver
    0x06: (0x66, 0),
    # Ganon's Tower's Red Mail neither: a piece of heart
    0x23: (0x63, 0),
}

# A Link to the Past enemies -> blend enemies: id, or (id, form). Ids 0x67 and up are
# A Link to the Past's own enemies (tmc/src/enemy/alttpEnemies.c); None: left out
ALTTP_STALFOS, ALTTP_EYEGORE, ALTTP_POPO, ALTTP_ANTIFAIRY, ARMOS_KNIGHT = 0x67, 0x68, 0x69, 0x6a, 0x6b
ALTTP_HINOX, LANMOLA, ALTTP_LEEVER, ALTTP_BEAMOS = 0x6f, 0x71, 0x73, 0x74
ALTTP_POKEY, ALTTP_GIBDO, GIANT_MOLDORM, ALTTP_BARI, ALTTP_HARDHAT, ALTTP_BUMPER = 0x76, 0x77, 0x78, 0x7a, 0x7b, 0x7c
# Minish Cap enemies used where A Link to the Past has a close relative
MC_WALL_MASTER = 0x1a
MC_CHUCHU, MC_DARK_NUT, MC_KEESE, MC_SLUGGULA, MC_WIZZROBE_FIRE, MC_WIZZROBE_ICE = 0x01, 0x05, 0x08, 0x10, 0x28, 0x29
ALTTP_ROPE, ALTTP_MOBLIN = 0x6c, 0x6d
ALTTP_DEBIRANDO_PIT, ALTTP_KODONGO, MINI_MOLDORM = 0x7d, 0x80, 0x81
HELMASAUR_KING, ARRGHUS, MOTHULA, ALTTP_SPIKE_BLOCK = 0x83, 0x86, 0x88, 0x8a
MINI_HELMASAUR, TERRORPIN, HOVER, KYAMERON, ZOL, FIRESNAKE, ALTTP_SHOOTER = 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91
BLIND, KHOLDSTARE, VITREOUS, TRINEXX = 0x92, 0x93, 0x95, 0x97
FREEZOR, PENGATOR, STALFOS_KNIGHT, WIZZROBE, LASER_EYE, CHAIN_CHOMP = 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0
ZAZAK, GIBO, FIRE_BAR, STALFOS_HEAD = 0xa1, 0xa2, 0xa3, 0xa4
AGAHNIM, GANON = 0xa5, 0xa7
ALTTP_SOLDIER, WALL_CANNON, FLYING_TILE, ALTTP_SPARK = 0xa9, 0xaa, 0xab, 0xac
ENEMIES = {
    'A7-Stalfos': ALTTP_STALFOS,
    '105-StalfosDrop': (ALTTP_STALFOS, 1),    # waits in the ceiling and drops on Link
    '118-StalfosAppear': (ALTTP_STALFOS, 1),
    '83-GreenRocklops': ALTTP_EYEGORE,          # zelda3's name for the green Eyegore
    '84-RedRocklops': (ALTTP_EYEGORE, 1),
    '4F-Tentacle': ALTTP_POPO,                  # and for the Popos
    '4E-Tentacle2': ALTTP_POPO,
    '15-Bubble': ALTTP_ANTIFAIRY,
    '82-4Bubbles': ALTTP_ANTIFAIRY,
    '53-ArmosKnight': ARMOS_KNIGHT,
    # Desert Palace and Tower of Hera
    '71-Leever': ALTTP_LEEVER,
    '61-Beamos': ALTTP_BEAMOS,
    '54-Lanmolas': LANMOLA,
    '09-Mouldrum': GIANT_MOLDORM,               # zelda3's name for the Moldorm boss
    '26-BlueOrb': ALTTP_HARDHAT,                # sprite 0x26 is the Hardhat Beetle
    '93-RedOrb': ALTTP_BUMPER,                  # sprite 0x93 is the Bumper
    '18-Moldorm': MINI_MOLDORM,                 # the small Moldorms
    '63-SandCrab1': (ALTTP_DEBIRANDO_PIT, 0),   # a Debirando in its sand pit
    '64-SandCrab2': (ALTTP_DEBIRANDO_PIT, 1),   # the red one that spits fireballs
    '86-Triceritops': ALTTP_KODONGO,            # Kodongos, the fire-breathing lizards
    # the Dark World dungeons
    '23-RedMiri': (ALTTP_BARI, 1),              # zelda3's name for the Baris
    '24-BlueMiri': ALTTP_BARI,
    '8B-Gibdo': ALTTP_GIBDO,
    '13-MiniHelmasaur': MINI_HELMASAUR,
    '8E-Shell': TERRORPIN,                      # Terrorpins: flip them with the Hammer
    '81-WaterBug': HOVER,                       # Hovers, the water striders
    '9A-Splash': KYAMERON,                      # Kyamerons, the bouncing water drops
    '8F-Blob': ZOL,
    '80-Lanmola': FIRESNAKE,                    # sprite 0x80 is the Firesnake
    '8A-SpikeBlock': ALTTP_SPIKE_BLOCK,
    'C5-Shooter': ALTTP_SHOOTER,                # Medusa statues
    'C6-4WayShooter': (ALTTP_SHOOTER, 1),
    '92-Helmasaur': HELMASAUR_KING,             # the Palace of Darkness's boss
    '8C-Arrghus': ARRGHUS,                      # the Swamp Palace's boss
    '8D-ArrghusFuzz': None,                     # Arrghus brings its own ring of puffs
    '88-Mothula': MOTHULA,                      # Skull Woods' boss
    '109-WallMaster': MC_WALL_MASTER,           # grabs Link and carries him to the entrance
    # the last Dark World dungeons: Thieves' Town, the Ice Palace, Misery Mire, Turtle Rock
    'CE-Blind': BLIND,
    'B7-BlindMan': None,                        # the maiden; Blind himself stands in the room
    'A2-KholdStare': KHOLDSTARE,                # it makes its own shell
    'A3': None, 'A4': None,                     # zelda3's Kholdstare shell and its ice
    'BD-Viterous': VITREOUS,                    # it brings its own small eyes
    'CB-TriNexx1': TRINEXX,
    'CC-TriNexx2': None, 'CD-TriNexx3': None,   # the side heads come with Trinexx
    'A1-IceMan': FREEZOR,                       # waits in the wall as a statue
    '99-Penguin': PENGATOR,
    '91-StalfosKnight': STALFOS_KNIGHT,
    '9B-Wizzrobe': WIZZROBE,
    '95-EyeLaser(Right)': (LASER_EYE, 0),       # the way it looks and fires
    '96-EyeLaser(Left)': (LASER_EYE, 1),
    '97-EyeLaser(Down)': (LASER_EYE, 2),
    '98-EyeLaser(Up)': (LASER_EYE, 3),
    'CA-Chomp': CHAIN_CHOMP,
    'A5-GreenLizard': (ZAZAK, 0),               # zelda3's name for the Zazaks
    'A6-RedLizard': (ZAZAK, 1),                 # the red one breathes fire
    'C3-Half-Bubble': GIBO,                     # Gibos: a shell and its nucleus
    '7E-FireBlade': (FIRE_BAR, 0),              # the fire bars, turning one way
    '7F-FireBlade2': (FIRE_BAR, 1),             # or the other
    '7C-StalfosHead': STALFOS_HEAD,
    'C7-FuzzyStack': ALTTP_POKEY,               # a Pokey stands in for the Pokey stack
    '7D-BigSpikeBlock': ALTTP_SPIKE_BLOCK,
    # Agahnim: the castle tower's, or (type 1, or in the Dark World) Agahnim 2 and his shadows
    '7A-Agahnim': AGAHNIM,
    '7B-OneShotMagicBal': None,                 # he casts his own balls
    # Ganon, in the Pyramid of Power (room 0x00): his trident, bats and torches are his own
    'D6-Ganon': GANON,
    # Ganon's Tower's soldiers (the Dark World's pig-faced ones, from the room's sprite set)
    '41-BlueSoldier': (ALTTP_SOLDIER, 0),       # the sword soldier
    '43-RedSpearSoldier': (ALTTP_SOLDIER, 1),   # the spearman, who thrusts
    # wall cannons, by the way they fire; they bring their own cannonballs
    '66-Cannon(Right)': (WALL_CANNON, 0),
    '67-Cannon(Left)': (WALL_CANNON, 1),
    '68-Cannon(Down)': (WALL_CANNON, 2),
    '69-Cannon(Up)': (WALL_CANNON, 3),
    '114-FloorTiles': FLYING_TILE,              # the tile room: it lifts the tiles itself
    '5B-WallBubble(L-R)': (ALTTP_SPARK, 0),     # zelda3's name for the Sparks: clockwise
    '5C-WallBubble(R-L)': (ALTTP_SPARK, 1),     # and the other way round
}
# enemies that live in the walls (statues) or on the water, not on the floor
ENEMY_CELLS = {
    'C5-Shooter': None, 'C6-4WayShooter': None,
    '81-WaterBug': ('~',), '9A-Splash': ('~',), '8C-Arrghus': ('~',),
    '95-EyeLaser(Right)': None, '96-EyeLaser(Left)': None, '97-EyeLaser(Down)': None,
    '98-EyeLaser(Up)': None, 'A1-IceMan': None,
    '66-Cannon(Right)': None, '67-Cannon(Left)': None, '68-Cannon(Down)': None, '69-Cannon(Up)': None,
    '114-FloorTiles': None,
    '5B-WallBubble(L-R)': None, '5C-WallBubble(R-L)': None,   # Sparks start against (or in) a wall
}

# Thieves' Town, the Ice Palace, Misery Mire, Turtle Rock and Ganon's Tower: until their own enemies
# and bosses are in ENEMIES, Minish Cap and blend enemies of a like kind stand in (an
# ENEMIES entry always wins over these)
STAND_INS = {
    '6F-Keese': MC_KEESE,
    '116-MiniBats': MC_KEESE,
    '6D-Rat': ALTTP_ROPE,                       # rats scurry like the Ropes
    'A5-GreenLizard': ALTTP_MOBLIN,             # Zazaks, the lizard soldiers
    'A6-RedLizard': ALTTP_MOBLIN,
    '91-StalfosKnight': ALTTP_STALFOS,
    '7C-StalfosHead': ALTTP_ANTIFAIRY,          # a skull drifting about the room
    '7D-BigSpikeBlock': ALTTP_SPIKE_BLOCK,
    'A1-IceMan': MC_WIZZROBE_ICE,               # Freezors: an ice spirit
    '99-Penguin': (MC_CHUCHU, 2),               # Pengators: a blue Chuchu slides about
    '9B-Wizzrobe': MC_WIZZROBE_FIRE,
    '20-BombSlug': MC_SLUGGULA,                 # Minish Cap's own bomb-dropping slugs
    'C7-FuzzyStack': ALTTP_POKEY,
    # the bosses: a gold Darknut stands in until each is built
    'CE-Blind': (MC_DARK_NUT, 2),
    'A2-KholdStare': (MC_DARK_NUT, 2),
    'BD-Viterous': (MC_DARK_NUT, 2),
    'CB-TriNexx1': (MC_DARK_NUT, 2),
}

DOOR_SMALL_KEY = 0x1c
DOOR_BIG_KEY = 0x1e
DOOR_EXIT = 0x0a
# entrance doors of several sizes, all leading out
DOOR_EXITS = (0x06, 0x0a, 0x0c, 0x0e, 0x10)
# locked doors over a staircase, upper and lower layer
DOOR_STAIR_KEY = (0x20, 0x22, 0x24, 0x26)

DIR_N, DIR_S, DIR_W, DIR_E = 0, 1, 2, 3
# Minish Cap door objects face 0 up, 1 right, 2 down, 3 left
MC_DIR = {DIR_N: 0, DIR_E: 1, DIR_S: 2, DIR_W: 3}

# the local flag that holds a Dark World dungeon's crystal switch state (their flag banks
# end at 0x7f, the boss's flag)
SWITCH_FLAG = 0x7e

# name: labels; area: the Minish Cap area it fills; rooms: A Link to the Past room ids
# (the first is the entrance, Minish Cap room 0); exit: where the way out leads;
# links: rooms whose door to the outside leads instead to another room's (a walk
# around outside in A Link to the Past); flag_base: first local flag it uses.
DUNGEONS = [
    dict(
        name='EasternPalace',
        rooms=[0xc9, 0xb9, 0xa9, 0xa8, 0xaa, 0x99, 0xb8, 0xba, 0xc8, 0xd8, 0xd9, 0xda],
        entrance=0xc9,
        # the entrance door leads back out to the cave mouth in the Eastern Hills
        area='AREA_EMPTY',
        exit=('AREA_HYRULE_FIELD', 'ROOM_HYRULE_FIELD_EASTERN_HILLS_CENTER', 0xa8, 0xb0),
        flag_base=0x10,
    ),
    dict(
        name='DesertPalace',
        rooms=[0x84, 0x74, 0x73, 0x75, 0x83, 0x85, 0x63, 0x53, 0x43, 0x33],
        entrance=0x84,
        # a cracked cave in the Trilby Highlands (was the Keese chest cave)
        area='AREA_NULL_1B',
        exit=('AREA_HYRULE_FIELD', 'ROOM_HYRULE_FIELD_TRILBY_HIGHLANDS', 0x88, 0x240),
        # the east wing's door leads round the outside to the back entrance
        links={0x85: 0x63, 0x63: 0x85},
        flag_base=0x40,
    ),
    dict(
        name='TowerOfHera',
        rooms=[0x77, 0x87, 0x31, 0x27, 0x17, 0x07],
        entrance=0x77,
        # a cracked cave high on Mt. Crenel (was the Chuchu pot chest cave)
        area='AREA_NULL_1C',
        exit=('AREA_MT_CRENEL', 'ROOM_MT_CRENEL_CENTER', 0x348, 0x70),
        flag_base=0x80,
        # its crystal switches work, as in A Link to the Past: this local flag holds
        # which color of pegs is raised (see tileset.Blend.room_pegs and blend.c)
        switch_flag=0xbd,
    ),
    dict(
        name='PalaceOfDarkness',
        rooms=[0x4a, 0x3a, 0x4b, 0x3b, 0x2a, 0x2b, 0x1a, 0x1b, 0x0a, 0x0b, 0x09, 0x19, 0x6a, 0x5a],
        entrance=0x4a,
        # the Eastern Palace's cave in the Eastern Hills, in the Dark World
        area='AREA_NULL_1D',
        exit=('AREA_HYRULE_FIELD', 'ROOM_HYRULE_FIELD_EASTERN_HILLS_CENTER', 0xa8, 0xb0),
        flag_base=0x10,
        switch_flag=SWITCH_FLAG,
        # leave out the quarters of its rooms Link never reaches
        trim=True,
    ),
    dict(
        name='SwampPalace',
        rooms=[0x28, 0x38, 0x37, 0x36, 0x35, 0x34, 0x54, 0x46, 0x26, 0x76, 0x16, 0x06],
        entrance=0x28,
        # room 0x66 above 0x76 is a side room of two floors at once (water channels under
        # walkways) that the 16px plan can't hold: it is left out and its door shut
        sealed={0x76: (DIR_N,)},
        # the Darknut cave in the Castor Wilds swamp, in the Dark World
        area='AREA_NULL_1E',
        exit=('AREA_CASTOR_WILDS', 'ROOM_CASTOR_WILDS_MAIN', 0x1a8, 0x1b8),
        flag_base=0x10,
        switch_flag=SWITCH_FLAG,
        trim=True,
    ),
    dict(
        name='SkullWoods',
        # in through the north wing, where the dungeon's three small keys can all be found
        # before a key door (the big chest room's own door, 0x58's, is the second way out)
        rooms=[0x57, 0x58, 0x56, 0x67, 0x68, 0x49, 0x39, 0x29],
        entrance=0x57,
        # the Great Fairy's hollow tree in the north-west Minish Woods, in the Dark World
        area='AREA_NULL_1F',
        exit=('AREA_MINISH_WOODS', 'ROOM_MINISH_WOODS_MAIN', 0x70, 0x58),
        # the west lobby's door leads through the woods to the skull's back door, as in
        # A Link to the Past. The back door's own hall (0x59) is left out: its walkways
        # and the maze under them are two floors at once, which the 16px plan can't hold,
        # so the way in comes straight into the Gibdo hall above it
        links={0x56: 0x49, 0x49: 0x56},
        outside={0x49: (DIR_S,)},
        # the Gibdo room's hole drops into Mothula's lair
        drops={0x39: 0x29},
        flag_base=0x10,
        trim=True,
    ),
    # Minish Cap has no empty areas left after 0x1F: the later dungeons fill 0x39-0x3C,
    # a run of areas the game never used (no rooms, exits or tile animations; every area
    # table holds an empty entry there). Their dungeon numbers (location - 23) run past the
    # save's 16 per-dungeon key slots, so dungeons 16 and up keep their keys in spare save
    # bytes (DUNGEON_KEYS and friends in save.h).
    dict(
        name='ThievesTown',
        rooms=[0xdb, 0xdc, 0xcb, 0xcc, 0xbb, 0xbc, 0xab, 0xac, 0x64, 0x65, 0x44, 0x45],
        entrance=0xdb,
        area='AREA_NULL_39',
        # Blind's door takes the big key, as A Link to the Past's boss doors do (the north
        # wing's own big key door has an open door beside it)
        locks={0xbc: {DIR_N: 'boss'}, 0xac: {DIR_S: 'boss'}},
        # the hub's four quarters are joined by raised walkways Link climbs onto by stairs;
        # the blend has one level, so a gap in the wall between quarters takes their place
        opens={0xdb: [(22, 0, 24, 3)], 0xcb: [(22, 29, 24, 32)], 0xdc: [(9, 0, 11, 3)], 0xcc: [(9, 29, 11, 32)]},
        # the stranger's house in the west of Hyrule Town, in the Dark World (the town is
        # A Link to the Past's Village of Outcasts there)
        exit=('AREA_HYRULE_TOWN', 'ROOM_HYRULE_TOWN_MAIN', 0x98, 0x17c),
        flag_base=0x10,
        switch_flag=SWITCH_FLAG,
        trim=True,
    ),
    dict(
        name='IcePalace',
        # (the fairy fountain 0x4F is left out: its door has no partner in 0x4E)
        rooms=[0x0e, 0x1e, 0x1f, 0x2e, 0x3e, 0x3f, 0x4e, 0x5e, 0x5f, 0x6e, 0x7e, 0x7f, 0x8e,
               0x9e, 0x9f, 0xae, 0xaf, 0xbe, 0xbf, 0xce, 0xde],
        entrance=0x0e,
        area='AREA_NULL_3A',
        # the cave on Lake Hylia's south shore, in the Dark World
        exit=('AREA_LAKE_HYLIA', 'ROOM_LAKE_HYLIA_MAIN', 0x148, 0x358),
        # the floor of the room below the big key door gives way to Kholdstare's lair; the
        # floor Link bombs open in 0x1E's octagon drops him into 0x3E; 0x7E's hole drops into
        # the big chest's room
        drops={0xce: 0xde, 0x1e: (0x3e, (24, 4)), 0x7e: 0x9e},
        locks={0xbe: {DIR_S: 'boss'}, 0xce: {DIR_N: 'boss'}},
        flag_base=0x10,
        switch_flag=SWITCH_FLAG,
        trim=True,
    ),
    dict(
        name='MiseryMire',
        rooms=[0x98, 0xd2, 0xc2, 0xc3, 0xc1, 0xb2, 0xb3, 0xd1, 0xb1, 0xa2, 0xa3, 0x97, 0xa1,
               0x90, 0x91, 0x92, 0x93, 0xa0],
        entrance=0x98,
        area='AREA_NULL_3B',
        # the gap past the entrance that A Link to the Past crosses by Hookshot or Pegasus
        # Boots gets a bridge (Minish Cap's Roc's Cape may not be Link's yet)
        opens={0x98: [(18, 20, 21, 21)]},
        # the four holes in 0x97's halls drop into 0xD1's west half, the way to its chest
        drops={0x97: (0xd1, None, (7, 24))},
        # the cave at the foot of the dark Veil Falls, opened with Ether
        exit=('AREA_VEIL_FALLS', 'ROOM_VEIL_FALLS_MAIN', 0x38, 0x1f8),
        flag_base=0x10,
        switch_flag=SWITCH_FLAG,
        trim=True,
    ),
    dict(
        name='TurtleRock',
        rooms=[0xd6, 0xc6, 0xb6, 0xc7, 0x15, 0xb7, 0x14, 0x24, 0x13, 0x04, 0x23, 0xb5, 0xc5,
               0xc4, 0xd5, 0xb4, 0xa4],
        entrance=0xd6,
        area='AREA_NULL_3C',
        # the cave on Mt. Crenel's summit, in the Dark World, opened with Quake
        exit=('AREA_MT_CRENEL', 'ROOM_MT_CRENEL_TOP', 0x328, 0x18),
        # doors to the rooms of the other entrances (0x33, 0xE5), left out, stay shut
        sealed={0x23: (DIR_S,), 0xd5: (DIR_S,)},
        # the track mazes of 0xB5 and the pipe room 0x14 keep only the bridges Link needs
        thin=(0xb5, 0x14),
        flag_base=0x10,
        switch_flag=SWITCH_FLAG,
        trim=True,
    ),
    # Ganon's Tower in the free area 0x3D. A Link to the Past's tower (26 rooms) runs past the
    # tileset's 2048 metatiles, so the blend keeps its main way up: the entrance hall, the
    # first floor's middle row (0x8B-0x8D) and 0x9D, with the map, compass and keys, the
    # Armos Knights and the big key in the basement, then the big key door and the floors
    # above (Lanmolas, Moldorm) up to Agahnim. Left out: the first floor's rooms north and
    # south of those (0x7B-0x7D, 0x9B, 0x9C: the invisible floor maze, the warp maze and the
    # chest rooms past them) and the empty rooms 0x8A and 0x9A; doors to them stay shut.
    dict(
        name='GanonsTower',
        rooms=[0x0c, 0x8c, 0x8b, 0x8d, 0x9d, 0x1c, 0x6b, 0x5b, 0x5c, 0x5d, 0x6d, 0x6c, 0xa5, 0x95,
               0x96, 0x3d, 0x4d, 0xa6, 0x4c, 0x1d, 0x0d],
        entrance=0x0c,
        area='AREA_NULL_3D',
        sealed={0x8c: (DIR_S,), 0x8b: (DIR_N, DIR_S), 0x8d: (DIR_N,), 0x9d: (DIR_W,)},
        # ways A Link to the Past takes by Hookshot, over a ledge or through a room left out:
        # bridges over 0x8B's pits to its map, a passage from 0x8C's north-east quarter to the
        # south-east one (reached from 0x9C in A Link to the Past), and in Moldorm's hall 0x4D
        # steps down off both ledges and a bridge to the platform with the way on
        opens={0x8b: [(12, 10, 14, 17), (12, 20, 14, 22)], 0x8c: [(23, 14, 25, 19)],
               0x4d: [(7, 9, 11, 10), (21, 9, 25, 10), (15, 17, 17, 22)]},
        # the Tower of Hera's cracked cave on Mt. Crenel, in the Dark World with the crystals
        exit=('AREA_MT_CRENEL', 'ROOM_MT_CRENEL_CENTER', 0x348, 0x70),
        flag_base=0x10,
        switch_flag=SWITCH_FLAG,
        trim=True,
    ),
    # The Pyramid of Power: Ganon's hall (room 0x00, its upper right quarter) in the free area
    # 0x3E. A Link to the Past's hall has no door: Link falls in through the hole Ganon
    # smashes in the Pyramid. Here the Dark World Castle Garden's castle door leads in once
    # Agahnim has fallen in Ganon's Tower (blend.c), Link lands where A Link to the Past puts
    # him, and steps in the hall's lower left corner lead back up to the garden.
    dict(
        name='Pyramid',
        rooms=[0x00],
        entrance=0x00,
        area='AREA_NULL_3E',
        arrive=(23, 8),
        up=(18, 10),
        exit=('AREA_CASTLE_GARDEN', 'ROOM_CASTLE_GARDEN_MAIN', 0x1f8, 0x38),
        flag_base=0x10,
        trim=True,
    ),
]


def read_room(room_id, lower=False):
    path = os.path.join(A.ROOMS, '%03x.bin' % room_id)
    data = open(path, 'rb').read()
    o = 0x2000 + 0x4000 + 5 + 12
    types = struct.unpack('<16H', data[o:o + 32])
    addrs = struct.unpack('<16H', data[o + 32:o + 64])
    dirs = struct.unpack('<16H', data[o + 64:o + 96])
    doors = []
    for t, a, d in zip(types, addrs, dirs):
        if t == 0 and a == 0:
            continue
        word = a >> 1
        if (word >= 0x1000) != lower:
            continue  # the other layer
        word &= 0xfff
        doors.append(dict(type=t & 0xff, x=word % 64, y=word // 64, dir=d & 3))
    return doors


class Plan:
    """One room, at 16px cells, ready to draw."""

    def __init__(self, room_id, sealed=(), outside=(), locks=None):
        self.id = room_id
        self.locks = locks or {}  # sides whose door the blend locks: {side: 'key' or 'boss'}
        self.sealed = sealed  # sides whose doors stay shut (to rooms the dungeon leaves out)
        self.outside = outside  # sides whose door leads outside instead (past a room left out)
        self.yaml = yaml.safe_load(open(os.path.join(YAML_DIR, 'dungeon-%d.yaml' % room_id)))
        # a room whose doors are all on its second layer has its walls there too (the
        # Tower of Hera's entrance hall); its first layer only scrolls along
        self.lower = not read_room(room_id) and bool(read_room(room_id, True))
        self.room = A.Room(room_id, self.lower)
        self.cells = list(self.room.cells)
        self.doors = []      # (kind, cell x, cell y, dir)
        self.stairs = []     # (cell x, cell y, stair index)
        self.chests = []     # (cell x, cell y, alttp item)
        self.big_chests = set()  # cells of the chests that are big ones (see big_chest)
        self.enemies = []    # (mc enemy, form, pixel x, pixel y, A Link to the Past cell)
        self.drops = []      # (cell x, cell y, room, landing cell): holes, and the way back up
        self.steps_up = []   # cells of steps the blend draws (the way back up from a drop)
        self.stair_cells = {}  # stairs -> the cells of their staircase
        self._doors()
        self._stairs()

    def finish(self, edges=()):
        """After the rooms are joined: `edges` are cells on the room's border that lead
        straight on into a neighboring room."""
        self._reach(list(edges))
        self._chests()
        self._enemies()
        self._clean()
        # a block or pot the plan dropped, alone in the walls around a room
        for i, c in enumerate(self.cells):
            x, y = i % 32, i // 32
            if c == A.FLOOR and not any(self.at(x + dx, y + dy) in (A.FLOOR, '+')
                                        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                self.cells[i] = A.SOLID

    def edge(self, side):
        """The cells along one border, in order."""
        return {DIR_N: [(i, 0) for i in range(32)], DIR_S: [(i, 31) for i in range(32)],
                DIR_W: [(0, i) for i in range(32)], DIR_E: [(31, i) for i in range(32)]}[side]

    def at(self, x, y):
        if 0 <= x < 32 and 0 <= y < 32:
            return self.cells[y * 32 + x]
        return '#'

    def put(self, x, y, c):
        if 0 <= x < 32 and 0 <= y < 32:
            self.cells[y * 32 + x] = c

    def _doors(self):
        for d in read_room(self.id, self.lower):
            x, y, dr = d['x'], d['y'], d['dir']
            # (a door between two quarters of the room stays: it leads to no other room)
            if dr in self.sealed and d['type'] not in DOOR_EXITS and not 12 < (y if dr in (DIR_N, DIR_S) else x) < 52:
                # a shut door's doorway is wall, so no doorway leads out to nothing
                if dr in (DIR_N, DIR_S):
                    cx = (x + 1) // 2
                    span = [(c, cy) for cy in (range(0, y // 2 + 2) if dr == DIR_N else range(y // 2, 32))
                            for c in (cx - 1, cx, cx + 1)]
                else:
                    cy = (y + 1) // 2
                    span = [(cx, c) for cx in (range(0, x // 2 + 2) if dr == DIR_W else range(x // 2, 32))
                            for c in (cy - 1, cy, cy + 1)]
                for p in span:
                    if self.at(*p) == A.DOOR:
                        self.put(p[0], p[1], A.SOLID)
                continue
            kind = {DOOR_SMALL_KEY: 'key', DOOR_BIG_KEY: 'boss'}.get(d['type'], 'open')
            if d['type'] in DOOR_EXITS or dr in self.outside:
                kind = 'exit'
            elif dr in self.locks and kind == 'open' and not 12 < (y if dr in (DIR_N, DIR_S) else x) < 52:
                kind = self.locks[dr]
            if d['type'] in DOOR_STAIR_KEY:
                # a locked door in front of stairs: the stairs stay, a key door stands
                # on the locked cell before them
                cx = (x + 1) // 2
                rows = [cy for cy in range(max(0, y // 2 - 2), min(32, y // 2 + 4))
                        if A.LOCKED in (self.at(cx, cy), self.at(cx - 1, cy))]
                if not rows:
                    continue
                for cy in rows:
                    # plain floor: a doorway's tiles would hold Link in the doorway and
                    # keep the stairs from taking him; the door object blocks the way
                    self.put(cx, cy, A.FLOOR)
                # the door on the locked cell nearest the room
                cy = max(rows) if dr == DIR_N else min(rows)
                self.doors.append(dict(kind='key', dir=dr, inner=(cx, cy), edge=None, other=self.id))
                continue
            if 12 < (y if dr in (DIR_N, DIR_S) else x) < 52 and kind in ('key', 'boss') and dr in (DIR_N, DIR_S):
                # a locked door between two halves of the same room: a one-cell way
                # through the wall with the Minish Cap door in it
                cx, cy = (x + 1) // 2, y // 2
                for j in range(-3, 4):
                    for c in (cx - 1, cx + 1):
                        if self.at(c, cy + j) in '+ksE':
                            self.put(c, cy + j, '#')
                    if self.at(cx, cy + j) not in (A.FLOOR, A.PIT, A.WATER, A.STAIRS):
                        self.put(cx, cy + j, '+')
                inner = (cx, cy + 1) if dr == DIR_N else (cx, cy)
                self.doors.append(dict(kind=kind, dir=dr, inner=inner, edge=None, other=self.id))
                continue
            if 12 < (y if dr in (DIR_N, DIR_S) else x) < 52:
                # a door between two halves of the same room: open the wall there
                cx, cy = x // 2, y // 2
                span = [(cx + i, cy + j) for i in (0, 1) for j in range(-2, 3)] if dr in (DIR_N, DIR_S) else \
                       [(cx + j, cy + i) for i in (0, 1) for j in range(-2, 3)]
                for p in span:
                    if self.at(*p) not in (A.PIT, A.WATER, A.STAIRS):
                        self.put(p[0], p[1], A.FLOOR)
                continue
            if dr in (DIR_N, DIR_S):
                cx = (x + 1) // 2
                rows = range(0, y // 2 + 2) if dr == DIR_N else range(y // 2, 32)
                for cy in rows:
                    for c in (cx - 1, cx, cx + 1):
                        if self.at(c, cy) in '+ksE':
                            self.put(c, cy, '#')
                    self.put(cx, cy, '+')
                # the door cell nearest the room's inside
                inner = (cx, max(rows)) if dr == DIR_N else (cx, min(rows))
                if kind == 'exit':
                    # walls along the way out, so the open ground outside stays out
                    for cy in rows:
                        if cy != inner[1]:
                            for c in (cx - 1, cx + 1):
                                if self.at(c, cy) in (A.FLOOR, A.ENTRANCE):
                                    self.put(c, cy, '#')
            else:
                cy = (y + 1) // 2
                cols = range(0, x // 2 + 2) if dr == DIR_W else range(x // 2, 32)
                for cx in cols:
                    for c in (cy - 1, cy, cy + 1):
                        if self.at(cx, c) in '+ksE':
                            self.put(cx, c, '#')
                    # the tileset has no side doorway Link can walk through, so a plain passage
                    self.put(cx, cy, A.FLOOR)
                inner = (max(cols), cy) if dr == DIR_W else (min(cols), cy)
            edge = (cx, 31 if dr == DIR_S else 0) if dr in (DIR_N, DIR_S) else (31 if dr == DIR_E else 0, cy)
            self.doors.append(dict(kind=kind, dir=dr, inner=inner, edge=edge, other=self.id + STEP[dr]))

    def _stairs(self):
        attr = self.room.attr
        seen = set()
        for cy in range(32):
            for cx in range(32):
                if self.cells[cy * 32 + cx] != A.STAIRS:
                    continue
                if (cx, cy) in seen:
                    continue
                # flood the cluster
                stack, cluster = [(cx, cy)], []
                while stack:
                    p = stack.pop()
                    if p in seen or self.at(*p) != A.STAIRS:
                        continue
                    seen.add(p)
                    cluster.append(p)
                    stack += [(p[0] + 1, p[1]), (p[0] - 1, p[1]), (p[0], p[1] + 1), (p[0], p[1] - 1)]
                index = 0
                for (x, y) in cluster:
                    for dy in (0, 1):
                        for dx in (0, 1):
                            a = attr[(y * 2 + dy) * 64 + x * 2 + dx]
                            if 0x30 <= a <= 0x37:
                                index = a & 3
                for p in cluster:
                    self.put(p[0], p[1], A.FLOOR)
                x, y = min(cluster, key=lambda p: (p[1], p[0]))
                self.stairs.append((x, y, index))
                self.stair_cells[(x, y, index)] = cluster

    def seal_stairs(self, stairs):
        """A staircase to a room the dungeon leaves out (the Swamp Palace's 0x16 up to the
        side room 0x66) is wall, with the passage from it to the room's edge, so it no longer
        leads out to nothing."""
        self.stairs.remove(stairs)
        cells = set(self.stair_cells.pop(stairs))
        stack = list(cells)
        while stack:
            x, y = stack.pop()
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                # (the passage: floor between the staircase and the room's edge)
                if (nx, ny) not in cells and self.at(nx, ny) == A.FLOOR and \
                        (nx in (0, 31) or ny in (0, 31) or min(ny, 31 - ny, nx, 31 - nx) < min(y, 31 - y, x, 31 - x)):
                    cells.add((nx, ny))
                    stack.append((nx, ny))
        for x, y in cells:
            self.put(x, y, A.SOLID)
        self.sealed_cells = getattr(self, 'sealed_cells', set()) | cells

    def _reach(self, edges):
        """A Link to the Past marks the unseen space around a room as floor; Minish Cap's
        camera shows all of it, so wall off any floor Link can't get to from a door or stairs."""
        # Doors, stairs and open edges lead in from the rest of the dungeon. A door back
        # into this same room (the Tower of Hera's locked one, whose inner side is worked
        # out wrong) and stairs beside it sit in a nook that A Link to the Past leaves open
        # to the floor around the room, so only their own few cells count.
        nooks = [d['inner'] for d in self.doors if d.get('other') == self.id]
        stairs = [(x, y) for (x, y, _) in self.stairs]
        near_nook = [q for q in stairs if any(abs(q[0] - n[0]) + abs(q[1] - n[1]) <= 2 for n in nooks)]
        nooks += near_nook
        main = [d['inner'] for d in self.doors if d.get('other') != self.id] + edges + \
            [q for q in stairs if q not in near_nook]
        if not main and not nooks:
            return
        # the way out from a door to the room's edge is a corridor: floor beside it
        # belongs to the space outside (the Tower of Hera stands in open floor)
        corridor = set()
        for d in self.doors:
            if d.get('edge'):
                (x0, y0), (x1, y1) = d['inner'], d['edge']
                if x0 == x1:
                    corridor |= {(x0, y) for y in range(min(y0, y1), max(y0, y1) + 1)}
                elif y0 == y1:
                    corridor |= {(x, y0) for x in range(min(x0, x1), max(x0, x1) + 1)}
        inners = {d['inner'] for d in self.doors}
        seen = set()

        def flood(seeds, near=None):
            stack = list(seeds)
            while stack:
                p = stack.pop()
                if p in seen or not (0 <= p[0] < 32 and 0 <= p[1] < 32):
                    continue
                if self.at(*p) in (A.SOLID, A.TORCH) and p not in seeds:
                    continue
                if near and min(abs(p[0] - q[0]) + abs(p[1] - q[1]) for q in near) > 2:
                    continue
                seen.add(p)
                nbrs = [(p[0] + 1, p[1]), (p[0] - 1, p[1]), (p[0], p[1] + 1), (p[0], p[1] - 1)]
                if p in corridor and p not in inners:
                    nbrs = [q for q in nbrs if q in corridor or q in seen]
                stack += nbrs
        flood(main)
        if not main:
            flood(nooks)
        else:
            flood([q for q in nooks if q not in seen], near=nooks)
        if not edges:
            # floor with a clear run to the room's edge is the space outside it
            # (a room with no open edge is closed off by walls everywhere but its doors)
            keep = set(stairs) | set(inners)
            def open_run(x, y, dx, dy):
                while 0 <= x < 32 and 0 <= y < 32:
                    if self.at(x, y) != A.FLOOR or (x, y) in keep:
                        return False
                    x, y = x + dx, y + dy
                return True
            for p in list(seen):
                if p not in corridor and p not in keep and self.at(*p) == A.FLOOR and \
                        any(open_run(p[0], p[1], dx, dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    seen.discard(p)
        # (blocks and pots too: the plan turns them to floor later, which would leave
        # pockets of floor in the walls, like Skull Woods's ring of blocks in room 0x58)
        for i, c in enumerate(self.cells):
            if c in (A.FLOOR, A.BLOCK, A.POT) and (i % 32, i // 32) not in seen:
                self.cells[i] = A.SOLID

    def _chests(self):
        items = self.yaml.get('Chests') or []
        found = {}
        for layer in (self.room.attr, self.room.lower):
            for cy in range(32):
                for cx in range(32):
                    for dy in (0, 1):
                        for dx in (0, 1):
                            a = layer[(cy * 2 + dy) * 64 + cx * 2 + dx]
                            if 0x58 <= a <= 0x5d and (a - 0x58) not in found:
                                found[a - 0x58] = (cx, cy)
        for n, item in enumerate(items):
            big = isinstance(item, str) and item.endswith('!')
            if isinstance(item, str):
                item = int(item.rstrip('!'))
            if n in found and self.at(*found[n]) in (A.FLOOR, A.CHEST, A.BIG_CHEST) and \
                    not any(found[n] == c[:2] for c in self.chests):
                cx, cy = found[n]
            else:
                # a chest that appears later in A Link to the Past: set it in the middle of the
                # floor, or beside a chest already there
                cx, cy = self.free_floor(*self.middle())
            self.put(cx, cy, A.FLOOR)
            self.chests.append((cx, cy, item))
            if big:
                self.big_chests.add((cx, cy))
        # keys an enemy dropped in A Link to the Past wait in a chest where it stood
        # and so do keys lying on the floor, on a statue Link dashes into, or under a pot
        loose = []
        for s in self.yaml.get('Sprites') or []:
            if len(s) > 4 and s[4] in ('drop_key', 'drop_big_key'):
                loose.append((s[0], s[1], 0x24 if s[4] == 'drop_key' else 0x32))
            elif s[3] in ('E4-Key', '3B-DashItem'):
                loose.append((s[0], s[1], 0x24))
        for s in self.yaml.get('Secrets') or []:
            if s[2] == '08-Key':
                loose.append((s[0] // 2, s[1] // 2, 0x24))
        for x, y, item in loose:
            if self.at(x, y) != A.FLOOR or any((x, y) == c[:2] for c in self.chests):
                # the nearest free floor
                spots = [(i % 32, i // 32) for i, c in enumerate(self.cells) if c == A.FLOOR and
                         not any((i % 32, i // 32) == q[:2] for q in self.chests)]
                if not spots:
                    continue
                x, y = min(spots, key=lambda p: abs(p[0] - x) + abs(p[1] - y))
            self.chests.append((x, y, item))

    def free_floor(self, x, y):
        """The floor cell nearest (x, y) with no chest on it, out in the open (floor all
        around) where there is such a place, so the chest blocks no passage."""
        taken = {q[:2] for q in self.chests}
        spots = [(i % 32, i // 32) for i, c in enumerate(self.cells) if c == A.FLOOR and
                 (i % 32, i // 32) not in taken]
        open_ = [p for p in spots if all(self.at(p[0] + dx, p[1] + dy) == A.FLOOR and
                                         (p[0] + dx, p[1] + dy) not in taken
                                         for dx in (-1, 0, 1) for dy in (-1, 0, 1))]
        return min(open_ or spots, key=lambda p: abs(p[0] - x) + abs(p[1] - y))

    def middle(self):
        floor = [(i % 32, i // 32) for i, c in enumerate(self.cells) if c == A.FLOOR]
        mx = sum(p[0] for p in floor) / len(floor)
        my = sum(p[1] for p in floor) / len(floor)
        return min(floor, key=lambda p: (p[0] - mx) ** 2 + (p[1] - my) ** 2)

    def _enemies(self):
        for s in self.yaml.get('Sprites') or []:
            mc = ENEMIES[s[3]] if s[3] in ENEMIES else STAND_INS.get(s[3])
            if mc is None:
                continue
            x, y = s[0], s[1]
            cells = ENEMY_CELLS.get(s[3], ())
            if self.at(x, y) != A.FLOOR and cells is not None and self.at(x, y) not in cells:
                continue
            form = 0
            if isinstance(mc, tuple):
                mc, form = mc
            px, py = x * 16 + 8, y * 16 + 8
            # A Link to the Past starts a swarm (Arrghus's puffs) on one spot and spreads it
            # out itself: here they stand in a ring around it, a few of them
            same = [e for e in self.enemies if e[4] == (x, y)]
            if same:
                if len(same) >= 5:
                    continue
                angle = len(same) * 6.2832 / 4
                px, py = int(px + 40 * math.cos(angle)), int(py + 40 * math.sin(angle))
                self.enemies.append((mc, form, px, py, (x, y)))
                continue
            self.enemies.append((mc, form, px, py, (x, y)))

    def _clean(self):
        """Reduce A Link to the Past's extra classes to the ones the tileset draws."""
        for i, c in enumerate(self.cells):
            if c in (A.LOCKED, A.SHUTTER, A.ENTRANCE):
                self.cells[i] = '#'
            elif c in (A.BLOCK, A.POT, A.CHEST, A.BIG_CHEST, A.STAIRS):
                self.cells[i] = A.FLOOR
            elif c == A.TORCH:
                self.cells[i] = '#'


STEP = {DIR_N: -16, DIR_S: 16, DIR_W: -1, DIR_E: 1}


OPPOSITE = {DIR_N: DIR_S, DIR_S: DIR_N, DIR_W: DIR_E, DIR_E: DIR_W}


def neighbor(r, side):
    if side == DIR_W and r & 15 == 0 or side == DIR_E and r & 15 == 15:
        return None
    return r + STEP[side]


def open_edges(plans):
    """Where A Link to the Past runs a room straight on into the next with no door:
    {room: [border cells that lead on]}."""
    edges = {r: [] for r in plans}
    for r, p in plans.items():
        for side in (DIR_N, DIR_S, DIR_W, DIR_E):
            other = neighbor(r, side)
            if other not in plans:
                continue
            q = plans[other]
            for a, b in zip(p.edge(side), q.edge(OPPOSITE[side])):
                if p.at(*a) == A.FLOOR and q.at(*b) == A.FLOOR:
                    edges[r].append(a)
    return edges


def layout(plans, edges):
    """Room positions in room units. A Link to the Past stacks floors on one grid, but
    Minish Cap scrolls between any rooms that touch, so each floor (rooms joined by
    doors or open edges) gets its own stretch of the map."""
    floor = {}
    for start in plans:
        if start in floor:
            continue
        n = len(set(floor.values()))
        stack = [start]
        while stack:
            r = stack.pop()
            if r in floor:
                continue
            floor[r] = n
            for d in plans[r].doors:
                other = d['other']
                if other in plans and d['kind'] != 'exit':
                    stack.append(other)
            for (x, y) in edges[r]:
                side = DIR_N if y == 0 else DIR_S if y == 31 else DIR_W if x == 0 else DIR_E
                stack.append(neighbor(r, side))
    col0 = min(r & 15 for r in plans)
    row0 = min(r >> 4 for r in plans)
    width = max(r & 15 for r in plans) - col0 + 2
    height = max(r >> 4 for r in plans) - row0 + 2
    # Minish Cap keeps positions in 16 bits: a map more than 32768 pixels (64 rooms) wide
    # would wrap around, so a dungeon of many floors (Ganon's Tower) lays them out in rows
    per_row = max(1, 63 // width)
    return {r: ((r & 15) - col0 + floor[r] % per_row * width, (r >> 4) - row0 + floor[r] // per_row * height)
            for r in plans}


BORDER = {DIR_N: 'TRANSITION_SHAPE_BORDER_NORTH', DIR_S: 'TRANSITION_SHAPE_BORDER_SOUTH',
          DIR_W: 'TRANSITION_SHAPE_BORDER_WEST', DIR_E: 'TRANSITION_SHAPE_BORDER_EAST'}


def add_drop(p, q, at=None, land=None):
    """A hole in room p that A Link to the Past drops through into room q (Skull Woods's
    way down to Mothula). Minish Cap pits only hurt Link and set him back, so each cell
    of the hole becomes a step that takes him down, keeping the hole's art; he lands where
    the hole lies above, and a step up in q's far corner takes him back beside the hole."""
    hole = [(i % 32, i // 32) for i, c in enumerate(p.cells) if c == A.PIT]
    if not hole:
        # a floor A Link to the Past breaks open (the Ice Palace's way down to Kholdstare):
        # the blend draws a step down in the open middle of the room instead
        hole = [p.free_floor(*(at or p.middle()))]
        p.steps_up.append(hole[0])
    floor_q = [(i % 32, i // 32) for i, c in enumerate(q.cells) if c == A.FLOOR]
    hx = sum(x for x, _ in hole) // len(hole)
    hy = sum(y for _, y in hole) // len(hole)
    land = land or min(floor_q, key=lambda c: abs(c[0] - hx) + abs(c[1] - hy))
    # a room with no door has only the floor he lands on: wall off the rest
    if not q.doors:
        q._reach([land])
    # the step back up: open floor (floor all round) in the lower left of the floor he
    # lands on
    seen, stack = set(), [land]
    while stack:
        c = stack.pop()
        if c in seen or q.at(*c) != A.FLOOR:
            continue
        seen.add(c)
        stack += [(c[0] + 1, c[1]), (c[0] - 1, c[1]), (c[0], c[1] + 1), (c[0], c[1] - 1)]
    near = [c for c in seen if c != land and
            all(q.at(c[0] + dx, c[1] + dy) == A.FLOOR for dx in (-1, 0, 1) for dy in (-1, 0, 1))]
    up = max(near, key=lambda c: (c[1] - c[0], c[1]))
    # back beside the hole, on the side nearest the room's floor below it
    floor_p = [(i % 32, i // 32) for i, c in enumerate(p.cells) if c == A.FLOOR]
    back = min(floor_p, key=lambda c: (min(abs(c[0] - x) + abs(c[1] - y) for x, y in hole), -c[1]))
    for x, y in hole:
        p.drops.append((x, y, q.id, land))
    q.drops.append((up[0], up[1], p.id, back))
    q.steps_up.append(up)


# Minish Cap's chest object, whose types 2 to 5 are its big chest
OBJ_CHEST_SPAWNER = 0xc


def big_chest(plan, cell):
    """Where Minish Cap's big chest stands for an A Link to the Past big chest at
    `cell` (its upper left): (x, y) in pixels, or None where there is no room for it. It
    is two cells wide, standing on the line between them, a row of cells deep."""
    cx, cy = cell
    others = {c[:2] for c in plan.chests} - {cell}
    for left in (cx, cx - 1):
        pair = ((left, cy), (left + 1, cy))
        if all(plan.at(*c) == A.FLOOR and c not in others for c in pair):
            return (left + 1) * 16, cy * 16 + 8
    return None


def link_cell(plan):
    """The step in a room's door to the outside that takes Link to a linked room: the
    door's last cell but one."""
    for d in plan.doors:
        if d['kind'] == 'exit':
            x, y = d['edge']
            dx, dy = {DIR_N: (0, 1), DIR_S: (0, -1), DIR_W: (1, 0), DIR_E: (-1, 0)}[d['dir']]
            return x + dx, y + dy
    raise RuntimeError('room %03x has no door to the outside' % plan.id)


def arrival(plan):
    """Where Link stands coming in through a room's door to the outside: just inside it."""
    for d in plan.doors:
        if d['kind'] == 'exit':
            x, y = d['inner']
            dx, dy = {DIR_N: (0, 1), DIR_S: (0, -1), DIR_W: (1, 0), DIR_E: (-1, 0)}[d['dir']]
            return (x + dx) * 16 + 8, (y + dy) * 16 + 8
    raise RuntimeError('room %03x has no door to the outside' % plan.id)


def generate():
    os.makedirs(OUT, exist_ok=True)
    tiler = autotile.Tiler('043', [('088', 'FortressOfWinds'), ('043', 'CastorDarknut')])
    maps_inc, rooms_inc, ents_inc, exits_h, entries_h = [], [], [], [], []
    pegs = []
    # BLEND_ONLY=Name[,Name]: draw only those dungeons (a quick look; the .inc files then
    # hold only them, so don't build the game from it)
    only = [n for n in os.environ.get('BLEND_ONLY', '').split(',') if n]
    for dg in DUNGEONS:
        name = dg['name']
        if only and name not in only:
            continue
        ids = dg['rooms']
        assert ids[0] == dg['entrance'], '%s: the entrance must be the first room' % name
        plans = {r: Plan(r, dg.get('sealed', {}).get(r, ()), dg.get('outside', {}).get(r, ()),
                         dg.get('locks', {}).get(r)) for r in ids}
        # cells the blend opens to the floor ({room: [(x0, y0, x1, y1), ...]}, exclusive):
        # ways A Link to the Past takes by a level Minish Cap doesn't have
        for r, rects in dg.get('opens', {}).items():
            for x0, y0, x1, y1 in rects:
                for y in range(y0, y1):
                    for x in range(x0, x1):
                        plans[r].put(x, y, A.FLOOR)
        # staircases to rooms the dungeon leaves out are walled up
        for r, p in plans.items():
            for st in list(p.stairs):
                dest = p.room.travel[st[2] + 1] if st[2] + 1 < len(p.room.travel) else 0
                if dest and dest not in plans:
                    p.seal_stairs(st)
        # bridges along tracks packed too close for Minish Cap's cells: only the ones Link needs
        for r in dg.get('thin', ()):
            calm.thin_tracks(plans[r])
        edges = open_edges(plans)
        for r in ids:
            plans[r].finish(edges[r])
        if 'arrive' in dg:
            # a hall with no door (the Pyramid): only the floor Link lands on is kept, and
            # steps lead back out
            p = plans[dg['entrance']]
            p._reach([dg['arrive']])
            p.steps_up.append(dg['up'])
        for r, dest in dg.get('drops', {}).items():
            # (a room, or (room, the cell where the blend breaks the floor open, the cell
            # Link lands on), either cell None to let the blend choose)
            dest, at, land = (dest + (None, None))[:3] if isinstance(dest, tuple) else (dest, None, None)
            add_drop(plans[r], plans[dest], at, land)
        index = {r: i for i, r in enumerate(ids)}
        place = layout(plans, edges)
        flag = dg['flag_base']
        door_flags = {}

        blend = tileset.Blend(name, trim=dg.get('trim', False)) if os.path.exists(os.path.join(alttpgfx.GFX, '%03x.gfx' % ids[0])) else None
        if blend is None:
            print('%s: no A Link to the Past room graphics, drawing with Minish Cap tiles' % name)
        elif dg.get('switch_flag'):
            blend.switch_rooms = {r for r in ids if any(a in tileset.PEG_ATTRS for a in plans[r].room.attr)}
        if blend and dg.get('switch_flag') == SWITCH_FLAG:
            # in the Dark World (the Tower of Hera's were set by hand), rooms with a switch and
            # no pegs of their own too; a room whose pegs would shut Link out of anything
            # keeps them flat (pegcheck)
            blend.switch_rooms |= {r for r in ids if blend.room_pegs(plans[r])['switches']}
            for r in sorted(blend.switch_rooms):
                why = pegcheck.check(plans[r], blend.room_pegs(plans[r]), edges[r])
                if why:
                    print('%s: room %03x keeps its pegs flat: %s' % (name, r, why))
                    blend.switch_rooms.discard(r)

        rooms_inc.append('gAreaRoomHeaders_%s::' % name)
        maps_list = []
        area_list = []
        exit_lists = []
        def mc_map(r):
            """The room in the Minish Cap metatiles the autotiler picks (their tile types
            stay; the blend redraws their art)."""
            p = plans[r]
            bottom, top, _ = tiler.draw(p.cells, 32, 32)
            bigs = {c: big_chest(p, c) for c in p.big_chests}
            for (cx, cy, item) in p.chests:
                if bigs.get((cx, cy)):
                    continue  # Minish Cap's big chest object stands on the floor there
                bottom[cy * 32 + cx] = TILE_CHEST
                top[cy * 32 + cx] = 0
            for (cx, cy, _) in p.stairs:
                bottom[cy * 32 + cx] = TILE_STAIRS
                top[cy * 32 + cx] = 0
            for (cx, cy, _, _) in p.drops:
                bottom[cy * 32 + cx] = TILE_STAIRS
                top[cy * 32 + cx] = 0
            if 'up' in dg and r == dg['entrance']:
                cx, cy = dg['up']
                bottom[cy * 32 + cx] = TILE_STAIRS
                top[cy * 32 + cx] = 0
            if r in dg.get('links', {}):
                # Minish Cap won't cross a room's border into a room of the same area, so a
                # linked way out ends in a step that takes Link there, as stairs do
                cx, cy = link_cell(p)
                bottom[cy * 32 + cx] = TILE_STAIRS
                top[cy * 32 + cx] = 0
            return bottom, top

        drawn = {}
        if blend:
            # the rooms with working pegs last, in a Dark World dungeon: their second look
            # takes what metatiles the tileset has left (see tileset.Blend.room)
            for r in sorted(ids, key=lambda r: r in blend.switch_rooms and dg.get('switch_flag') == SWITCH_FLAG):
                drawn[r] = blend.room(plans[r], *mc_map(r))
            # the tileset (it may merge metatiles that came out alike: blend.remap)
            tileset_inc = blend.write(OUT)
            remap = blend.remap or {}
            for r in ids:
                drawn[r] = ([remap.get(m, m) if m < 0x4000 else m for m in drawn[r][0]], drawn[r][1])
            for r, (switches, swaps) in blend.pegs.items():
                # (a cell whose two looks came out alike needs no swap)
                blend.pegs[r] = (switches, [(c, remap.get(m0, m0), remap.get(m1, m1), s0, s1)
                                            for (c, m0, m1, s0, s1) in swaps
                                            if remap.get(m0, m0) != remap.get(m1, m1)])
        for r in ids:
            p = plans[r]
            i = index[r]
            bigs = {c: big_chest(p, c) for c in p.big_chests}
            bottom, top = drawn[r] if blend else mc_map(r)
            for layer, cells in (('bottom', bottom), ('top', top)):
                open(os.path.join(OUT, '%s_%02d_%s.bin' % (name, i, layer)), 'wb').write(
                    struct.pack('<%dH' % len(cells), *cells))
                maps_inc.append('\t.align 2\ng%s_%02d_%s::\n\t.incbin "custom/gen/%s_%02d_%s.bin"' % (
                    name, i, layer, name, i, layer))
            maps_list.append('g%s_%02d' % (name, i))
            rooms_inc.append('\troom_header 0x%x, 0x%x, 0x200, 0x200, 0' % (
                place[r][0] * 32, place[r][1] * 32))

            # entities
            label = '%s_%02d' % (name, i)
            ents_inc.append('Entities_%s::' % label)
            for d in p.doors:
                if d['kind'] in ('key', 'boss'):
                    other = d['other']
                    key = (min(r, other), max(r, other), d['dir'] in (DIR_N, DIR_S))
                    if other == r:
                        # inside one room: each door by where it stands
                        key += (d['inner'][0],)
                    if key not in door_flags:
                        door_flags[key] = flag
                        flag += 1
                    x, y = d['inner']
                    sub = 0x8 if d['kind'] == 'key' else 0x39
                    param_a = (0x8 | MC_DIR[d['dir']]) if d['kind'] == 'key' else MC_DIR[d['dir']]
                    ents_inc.append('\tobject_raw subtype=0x%x, x=0x%x, y=0x%x, paramA=0x%x, paramC=0x%x' % (
                        sub, x * 16 + 8, y * 16 + 8, param_a, (door_flags[key] << 16) | 0xffff))
            # big chests: Minish Cap's big chest object, shown from the start (type 4), its
            # local flag the one its tile entity below gives the item for
            chest_flags = {}
            for n, (cx, cy, item) in enumerate(p.chests):
                chest_flags[(cx, cy)] = flag + n
                if bigs.get((cx, cy)):
                    x, y = bigs[(cx, cy)]
                    ents_inc.append('\tobject_raw subtype=0x%x, x=0x%x, y=0x%x, paramA=0x4, paramB=0x%x' % (
                        OBJ_CHEST_SPAWNER, x, y, flag + n))
            ents_inc.append('\tentity_list_end\n')
            ents_inc.append('Enemies_%s::' % label)
            for (mc, form, x, y, _) in p.enemies:
                ents_inc.append('\tenemy_raw subtype=0x%x, paramA=0x%x, x=0x%x, y=0x%x' % (mc, form, x, y))
            ents_inc.append('\tentity_list_end\n')
            ents_inc.append('TileEntities_%s::' % label)
            for (cx, cy, item) in p.chests:
                mc, sub = ITEMS.get(item, (0x56, 0))
                assert chest_flags[(cx, cy)] == flag
                if bigs.get((cx, cy)):
                    # (where the big chest stands, for the dungeon map)
                    ents_inc.append('\ttile_entity type=0x3, paramA=0x%x, paramB=0x%x, paramC=0x%x, paramD=0x%x' % (
                        (flag, (sub << 8) | mc) + bigs[(cx, cy)]))
                else:
                    ents_inc.append('\ttile_entity type=0x2, paramA=0x%x, paramB=0x%x, paramC=0x%x' % (
                        flag, (sub << 8) | mc, cy * 64 + cx))
                flag += 1
            ents_inc.append('\ttile_entity_list_end\n')
            ents_inc.append('Room_%s::\n\t.4byte Entities_%s, 0, Enemies_%s, TileEntities_%s, 0, 0, 0, 0\n' % (
                label, label, label, label))
            area_list.append('Room_%s' % label)

            # exits: stairs, and the way out
            exits = []
            for (cx, cy, j) in p.stairs:
                dest = p.room.travel[j + 1] if j + 1 < len(p.room.travel) else 0
                if dest not in plans:
                    continue
                q = plans[dest]
                land = None
                # when two staircases join the same rooms, the first in one room (top to
                # bottom) leads to the first in the other
                mine = sorted((y, x) for (x, y, k) in p.stairs if p.room.travel[k + 1] == dest)
                theirs = sorted((dy, dx) for (dx, dy, dj) in q.stairs if q.room.travel[dj + 1] == r)
                if theirs:
                    n = mine.index((cy, cx))
                    land = theirs[min(n, len(theirs) - 1)][::-1]
                if land is None and q.stairs:
                    land = q.stairs[0][:2]
                if land is None:
                    continue
                lx, ly = land
                # step off the stairs onto the floor below them
                for ox, oy in ((0, 1), (0, -1), (1, 0), (-1, 0), (0, 2)):
                    if q.at(lx + ox, ly + oy) == A.FLOOR:
                        lx, ly = lx + ox, ly + oy
                        break
                exits.append('\t{ WARP_TYPE_AREA, 0x%x, 0x%x, 0x%x, 0x%x, TRANSITION_SHAPE_AREA_12x12, %s, %d, 1, '
                             'TRANSITION_TYPE_NORMAL, 0x4, 0x0, 0x0, 0x0 },' % (
                                 cx * 16 + 8, cy * 16 + 8, lx * 16 + 8, ly * 16 + 8, dg['area'], index[dest]))
            # a hole is one exit per row, as wide as the hole (no gap between its cells)
            rows = {}
            for (cx, cy, dest, land) in p.drops:
                rows.setdefault((cy, dest, land), []).append(cx)
            for (cy, dest, (lx, ly)), xs in sorted(rows.items()):
                # (wide enough that Link, sliding along the hole's edge, still drops in)
                shape = '12x12' if len(xs) == 1 else '44x12'
                exits.append('\t{ WARP_TYPE_AREA, 0x%x, 0x%x, 0x%x, 0x%x, TRANSITION_SHAPE_AREA_%s, %s, %d, 1, '
                             'TRANSITION_TYPE_NORMAL, 0x4, 0x0, 0x0, 0x0 },' % (
                                 (min(xs) + max(xs) + 1) * 8, cy * 16 + 8, lx * 16 + 8, ly * 16 + 8, shape,
                                 dg['area'], index[dest]))
            if 'up' in dg and r == dg['entrance']:
                # the steps out of a hall with no door
                cx, cy = dg['up']
                area, room, x, y = dg['exit']
                exits.append('\t{ WARP_TYPE_AREA, 0x%x, 0x%x, 0x%x, 0x%x, TRANSITION_SHAPE_AREA_12x12, %s, %s, 1, '
                             'TRANSITION_TYPE_NORMAL, 0x4, 0x0, 0x0, 0x0 },' % (
                                 cx * 16 + 8, cy * 16 + 8, x, y, area, room))
            for d in p.doors:
                if d['kind'] != 'exit':
                    continue
                link = dg.get('links', {}).get(r)
                if link is not None:
                    # straight on to the linked room's own door to the outside
                    x, y = arrival(plans[link])
                    cx, cy = link_cell(p)
                    # wide: the step snaps Link sideways as he walks on (a doorway's walls
                    # keep him from coming at it anywhere else)
                    exits.append('\t{ WARP_TYPE_AREA, 0x%x, 0x%x, 0x%x, 0x%x, TRANSITION_SHAPE_AREA_44x12, %s, %d, 1, '
                                 'TRANSITION_TYPE_NORMAL, 0x4, 0x0, 0x0, 0x0 },' % (
                                     cx * 16 + 8, cy * 16 + 8, x, y, dg['area'], index[link]))
                    break
                else:
                    area, room, x, y = dg['exit']
                exits.append('\t{ WARP_TYPE_BORDER, 0x0, 0x0, 0x%x, 0x%x, %s, %s, %s, 1, '
                             'TRANSITION_TYPE_NORMAL, 0x4, 0x0, 0x0, 0x0 },' % (
                                 x, y, BORDER[d['dir']], area, room))
                break
            exit_lists.append('const Transition gExitList_%s[] = {\n%s\n\tTransitionListEnd,\n};' % (
                label, '\n'.join(exits)))

        rooms_inc.append('\t.2byte 0xFFFF\n')
        maps_inc.append('')
        if blend:
            maps_inc += tileset_inc
            if blend.pegs:
                assert flag <= dg['switch_flag'], '%s: the switch flag is taken' % name
                pegs.append((name, dg['area'], dg['switch_flag'], {index[r]: v for r, v in blend.pegs.items()}))
        else:
            # the Minish Cap tiles the rooms were drawn with
            cd = lambda k: '(gAreaTileSet%s_CastorDarknut_%s - gMapData)' % k
            maps_inc += ['\t.align 2\ngAreaTileSets_%s::\n\t.4byte gAreaTileSet_CastorDarknut_0' % name,
                         'gAreaTiles_%s::' % name,
                         '\ttiles_bottom %s, 0x3868, 1' % cd(('', 'bottom')),
                         '\ttiles_top %s, 0x3850, 1' % cd(('', 'top')),
                         '\ttile_types_bottom %s, 0xE1A, 1' % cd(('Types', 'bottom')),
                         '\ttile_types_top %s, 0xE14, 1, 1' % cd(('Types', 'top'))]
        for i, m in enumerate(maps_list):
            maps_inc.append('\t.align 2\ngAreaRoomMap_%s::' % m)
            maps_inc.append('\tmap_bottom (%s_bottom - gMapData), 0x800, 0' % m)
            maps_inc.append('\tmap_top (%s_top - gMapData), 0x800, 0, 1' % m)
        maps_inc.append('\t.align 2\ngAreaRoomMaps_%s::' % name)
        maps_inc += ['\t.4byte gAreaRoomMap_%s' % m for m in maps_list]
        ents_inc.append('Area_%s::' % name)
        ents_inc += ['\t.4byte %s' % a for a in area_list]
        ents_inc.append('')
        exits_h += exit_lists
        exits_h.append('const Transition* const gExitLists_%s[] = {\n%s\n};' % (
            name, '\n'.join('\tgExitList_%s_%02d,' % (name, i) for i in range(len(ids)))))
        land = tuple(c * 16 + 8 for c in dg['arrive']) if 'arrive' in dg else arrival(plans[dg['entrance']])
        entries_h.append('#define ENTRY_%s 0x%x, 0x%x' % ((name,) + land))
        print('%s: %d rooms, flags 0x%x-0x%x' % (name, len(ids), dg['flag_base'], flag - 1))
        print('  ' + ', '.join('%d: %03x' % (i, r) for i, r in enumerate(ids)))

    # the Temple of Bonds, the co-op temple: chambers from all of them (shrine.py)
    shrine_h = None
    if not only or shrine.NAME in only:
        shrine_h = shrine.generate(None, tiler, OUT, maps_inc, rooms_inc, ents_inc, exits_h, entries_h)

    def write(fname, lines):
        open(os.path.join(OUT, fname), 'w').write(
            '@ generated by blend/dungeons/build.py from the user\'s own cartridge; do not commit\n'.replace(
                '@', '//' if fname.endswith('.h') else '@') + '\n'.join(lines) + '\n')

    write('maps.inc', maps_inc)
    write('rooms.inc', rooms_inc)
    write('entities.inc', ents_inc)
    write('exits.h', exits_h)
    # where each dungeon's way in from the overworld lands (for transitions.c)
    write('entries.h', entries_h)
    # the crystal switch rooms (for blend.c)
    write('pegs.h', tileset.peg_header(pegs))
    if shrine_h:
        write('shrine.h', shrine_h)


if __name__ == '__main__':
    import sys
    if sys.argv[1:] == ['--rooms']:
        # for make-dungeons.sh: each dungeon's entrance room, then its rooms
        for dg in DUNGEONS:
            print(' '.join('%x' % r for r in [dg['entrance']] + dg['rooms']))
        # the Temple of Bonds's chambers, each with its own temple's graphics
        for spec in shrine.ROOMS:
            print('%x %x' % (spec['entrance'], spec['src']))
    else:
        generate()
