/**
 * @file blendOverworld.c
 *
 * @brief A Link to the Past's dungeon fronts and landmarks on Minish Cap's overworld,
 * for the Minish Cap x A Link to the Past blend.
 *
 * blend/overworld/exteriors.py cuts each dungeon's front (the Eastern Palace's
 * statues, the Palace of Darkness, the skull of Skull Woods...) out of A Link to the
 * Past's overworld, re-lit in the blended style, and lays it into the Minish Cap room
 * whose door leads to that dungeon. It writes, per room and world: the characters
 * (into character slots the room and its neighbours never use), one or two palette
 * rows (that they never use either), the new metatiles (into the area's unused
 * metatile slots), the room's new map, and its doors.
 *
 * Here the room loads as usual, then the exterior is laid over it, only in the world
 * it belongs to. A door shows sealed (solid, closed art) until its dungeon can be
 * entered. The characters and colors stay in video memory after Link walks on; the
 * room's neighbours never use them, and as soon as he reaches a room that might, the
 * area's own tileset is loaded again.
 */
#include "blend.h"
#include "area.h"
#include "common.h"
#include "fade.h"
#include "flags.h"
#include "map.h"
#include "room.h"
#include "main.h"
#include "save.h"

typedef struct {
    u16 cell;   /**< packed room map index: y * room width + x */
    u16 sealed; /**< metatile shown while the way in is shut */
    u8 dungeon; /**< dungeon area behind the door, or 0 */
    u8 caveFlag; /**< overworld local flag (bank 1) that opens it, or 0 */
    u16 pad;
} BlendExteriorDoor;

typedef struct {
    u8 area;
    u8 room;
    u8 world;    /**< 1 Light World, 2 Dark World, 3 both */
    u8 doorCount;
    u16 rowMask; /**< background palette rows it brings */
    u16 metaCount;
    u16 charCount;
    u16 cellCount; /**< room width * height */
    const u8* chars;      /**< charCount characters, 4bpp */
    const u16* charSlots; /**< where each goes, in characters from 0x6000000 */
    const u16* palette;   /**< 16 colors per row in rowMask, lowest row first */
    const u16* metaSlots; /**< bottom metatile numbers */
    const u16* metas;     /**< four entries per metatile */
    const u16* types;     /**< a tile type per metatile */
    const u16* mapBottom; /**< the room's whole map, packed */
    const u16* mapTop;
    const BlendExteriorDoor* doors;
    const u8* neighbours; /**< rooms of the area that never use its characters, 0xff ends */
} BlendExterior;

typedef struct {
    u8 dirty;     /**< an exterior's characters and colors are in video memory */
    u8 area;      /**< whose */
    u8 room;
    u8 world;
    u16 rowMask;  /**< palette rows of the exterior shown now (they keep their colors in the Dark World) */
    u16 pad;
    const BlendExterior* active;
} BlendExteriorState;

extern const BlendExterior gBlendExteriors[];
extern BlendExteriorState gBlendExterior;

bool32 Blend_CanEnterDungeon(u32 area);
void LoadMapData(MapDataDefinition* dataDefinition);

static u32 CurrentWorld(void) {
    return BLEND_DARK_WORLD ? 2 : 1;
}

static const BlendExterior* FindExterior(u32 area, u32 room, u32 world) {
    const BlendExterior* e;
    for (e = gBlendExteriors; e->area != 0xff; e++) {
        if (e->area == area && e->room == room && (e->world & world)) {
            return e;
        }
    }
    return NULL;
}

static bool32 IsNeighbour(const BlendExterior* e, u32 room) {
    const u8* n;
    for (n = e->neighbours; *n != 0xff; n++) {
        if (*n == room) {
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * Before the room's own map loads: if an exterior left its characters and colors
 * behind and this room might use those slots, load the area's tileset again.
 */
void Blend_ExteriorBeforeMap(void) {
    BlendExteriorState* s = &gBlendExterior;
    const BlendExterior* e;
    s->active = NULL;
    s->rowMask = 0;
    if (!s->dirty) {
        return;
    }
    e = FindExterior(s->area, s->room, s->world);
    if (e != NULL && gRoomControls.area == s->area && gRoomControls.room != s->room &&
        IsNeighbour(e, gRoomControls.room)) {
        return; // a neighbour: it never uses them
    }
    s->dirty = 0;
    if (gArea.pCurrentRoomInfo->tileSet != NULL) {
        LoadMapData(gArea.pCurrentRoomInfo->tileSet);
    }
}

static bool32 IsDoorSealed(const BlendExteriorDoor* d) {
    if (d->caveFlag != 0 && !CheckLocalFlagByBank(FLAG_BANK_1, d->caveFlag)) {
        return TRUE;
    }
    return d->dungeon != 0 && !Blend_CanEnterDungeon(d->dungeon);
}

/** After the room's own map loads: lay the exterior over it. */
void Blend_ExteriorAfterMap(void) {
    BlendExteriorState* s = &gBlendExterior;
    const BlendExterior* e;
    u32 i, row, n;
    e = FindExterior(gRoomControls.area, gRoomControls.room, CurrentWorld());
    if (e == NULL) {
        return;
    }
    for (i = 0; i < e->charCount; i++) {
        MemCopy(e->chars + i * 32, (u8*)0x06000000 + e->charSlots[i] * 32, 32);
    }
    n = 0;
    for (row = 0; row < 16; row++) {
        if (e->rowMask & (1 << row)) {
            MemCopy(e->palette + n * 16, gPaletteBuffer + row * 16, 32);
            USE_PALETTE(row);
            n++;
        }
    }
    for (i = 0; i < e->metaCount; i++) {
        u32 m = e->metaSlots[i];
        MemCopy(e->metas + i * 4, &gMapBottom.subTiles[m * 4], 8);
        gMapBottom.tileTypes[m] = e->types[i];
    }
    MemCopy(e->mapBottom, gMapBottom.mapData, e->cellCount * 2);
    MemCopy(e->mapTop, gMapTop.mapData, e->cellCount * 2);
    for (i = 0; i < e->doorCount; i++) {
        const BlendExteriorDoor* d = &e->doors[i];
        if (IsDoorSealed(d)) {
            gMapBottom.mapData[d->cell] = d->sealed;
        }
    }
    s->active = e;
    s->rowMask = e->rowMask;
    s->dirty = 1;
    s->area = e->area;
    s->room = e->room;
    s->world = CurrentWorld();
}

/**
 * Puts the exterior's colors back if something loaded a palette over them since (a
 * room's setup loading its weather colors, the storm on the mountain top).
 */
void Blend_ExteriorRefreshColors(void) {
    const BlendExterior* e;
    u32 row, n;
    if (!Blend_ExteriorActive()) {
        return;
    }
    e = gBlendExterior.active;
    n = 0;
    for (row = 0; row < 16; row++) {
        if (e->rowMask & (1 << row)) {
            const u16* want = e->palette + n * 16;
            u16* have = gPaletteBuffer + row * 16;
            if (have[1] != want[1] || have[2] != want[2] || have[15] != want[15]) {
                MemCopy(want, have, 32);
                USE_PALETTE(row);
            }
            n++;
        }
    }
}

/** An exterior is laid over the room Link is in. */
bool32 Blend_ExteriorActive(void) {
    const BlendExterior* e = gBlendExterior.active;
    return e != NULL && e->area == gRoomControls.area && e->room == gRoomControls.room;
}

/** The exterior's palette rows wear their own Dark World colors already. */
bool32 Blend_ExteriorKeepsColors(u32 palIdx) {
    return palIdx < 16 && (gBlendExterior.rowMask & (1 << palIdx)) && Blend_ExteriorActive();
}
