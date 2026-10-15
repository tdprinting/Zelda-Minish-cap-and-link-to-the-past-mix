/**
 * @file blendShadows.c
 *
 * @brief Soft shadows on the overworld, for the Minish Cap x A Link to the Past blend.
 *
 * Trees, cliffs, buildings and walls cast soft shadows down and to the right.
 * The shadows live on background 3, which the hardware blends over the ground
 * (and over Link and the other sprites below it), so they read as see-through
 * shade rather than paint. The top layer (tree tops, roofs) stays in front.
 *
 * blend/overworld/build.py works the shadows out from each room at build time:
 * per room one byte per 8x8 cell (character number, mirror bits), per tileset a
 * small set of shadow characters that go into character slots the tileset
 * never uses, and three shadow colors in unused palette slots.
 *
 * The layer only runs where nothing else wants background 3 or the blend unit;
 * whenever the game uses either (cloud cover, light rays, cutscenes, menus) the
 * shadows step aside and come back once it is done.
 */
#include "blend.h"
#include "area.h"
#include "common.h"
#include "room.h"
#include "screen.h"
#include "vram.h"
#include "main.h"

typedef struct {
    u8 row;   /**< palette row of the shadow colors */
    u8 count; /**< characters, including the blank one */
    u16 pad;
    const u16* slots; /**< character number -> tile number for background 3 (character base 2) */
    const u8* gfx;    /**< count characters, 4bpp */
} BlendShadowSet;

typedef struct {
    u8 area;
    u8 room;
    u8 set;
    u8 pad;
    u16 width;  /**< in 8x8 cells */
    u16 height; /**< in 8x8 cells */
    const u8* cells; /**< bits 0-5 character number, bit 6 h-flip, bit 7 v-flip */
} BlendShadowRoom;

extern const BlendShadowSet gBlendShadowSets[];
extern const BlendShadowRoom gBlendShadowRooms[];

typedef struct {
    const BlendShadowRoom* room;
    u16 key;
    u8 active;
    u8 pad;
    s16 cellX;
    s16 cellY;
    const BlendShadowSet* loaded;
} BlendShadowState;

extern BlendShadowState gBlendShadow;

#define SHADOW_BG3_CONTROL (BGCNT_PRIORITY(1) | BGCNT_CHARBASE(2) | BGCNT_SCREENBASE(30))
#define SHADOW_FX_CONTROL \
    (BLDCNT_TGT1_BG3 | BLDCNT_EFFECT_BLEND | BLDCNT_TGT2_BG2 | BLDCNT_TGT2_OBJ | BLDCNT_TGT2_BD)
#define SHADOW_ALPHA BLDALPHA_BLEND(5, 11)
#define SHADOW_VRAM ((u8*)0x06008000)

static const BlendShadowRoom* FindRoom(u32 area, u32 room) {
    const BlendShadowRoom* r;
    for (r = gBlendShadowRooms; r->area != 0xff; r++) {
        if (r->area == area && r->room == room) {
            return r;
        }
    }
    return NULL;
}

bool32 Blend_RoomHasShadows(void) {
    return FindRoom(gRoomControls.area, gRoomControls.room) != NULL;
}

/** The shadow characters are still in video memory (a tileset load or a menu may have replaced them). */
static bool32 CharsPresent(const BlendShadowSet* set) {
    u32 i;
    for (i = 1; i < set->count; i += 7) {
        const u32* want = (const u32*)(set->gfx + i * 32);
        const u32* have = (const u32*)(SHADOW_VRAM + set->slots[i] * 32);
        if (want[0] != have[0] || want[3] != have[3] || want[6] != have[6]) {
            return FALSE;
        }
    }
    return TRUE;
}

static void LoadChars(const BlendShadowSet* set) {
    u32 i;
    for (i = 0; i < set->count; i++) {
        MemCopy(set->gfx + i * 32, SHADOW_VRAM + set->slots[i] * 32, 32);
    }
}

static void TurnOff(void) {
    if (!gBlendShadow.active) {
        return;
    }
    gBlendShadow.active = 0;
    if (gScreen.bg3.control == SHADOW_BG3_CONTROL) {
        gScreen.lcd.displayControl &= ~DISPCNT_BG3_ON;
    }
    if (gScreen.controls.layerFXControl == SHADOW_FX_CONTROL) {
        gScreen.controls.layerFXControl = 0;
    }
}

/** Writes the visible window of the room's shadow cells into background 3's map. */
static void Draw(const BlendShadowRoom* r, const BlendShadowSet* set, s32 x0, s32 y0) {
    s32 x, y;
    u32 rowBits = set->row << 12;
    u32 blank = set->slots[0] | rowBits;
    for (y = y0; y < y0 + 21; y++) {
        u16* line = &gBG3Buffer[(y & 31) * 32];
        const u8* src = r->cells + y * r->width;
        bool32 inside = y >= 0 && y < r->height;
        for (x = x0; x < x0 + 31; x++) {
            u32 entry = blank;
            if (inside && x >= 0 && x < r->width) {
                u32 c = src[x];
                if (c != 0) {
                    entry = set->slots[c & 0x3f] | ((c & 0xc0) << 4) | rowBits;
                }
            }
            line[x & 31] = entry;
        }
    }
    gScreen.bg3.updated = 1;
}

/** Runs every gameplay frame, after the camera and the room's managers have moved. */
void Blend_ShadowUpdate(void) {
    const BlendShadowRoom* r;
    const BlendShadowSet* set;
    s32 sx, sy, cx, cy;
    u32 key = (gRoomControls.area << 8) | gRoomControls.room;

    if (key != gBlendShadow.key) {
        gBlendShadow.key = key;
        gBlendShadow.room = FindRoom(gRoomControls.area, gRoomControls.room);
        gBlendShadow.cellX = 0x7fff;
        TurnOff();
    }
    r = gBlendShadow.room;
    if (r == NULL || gRoomControls.scrollAction != 1) {
        TurnOff();
        return;
    }
    if (gBlendShadow.active) {
        // something else took background 3 or the blend unit: step aside
        if (gScreen.bg3.control != SHADOW_BG3_CONTROL || gScreen.controls.layerFXControl != SHADOW_FX_CONTROL ||
            !(gScreen.lcd.displayControl & DISPCNT_BG3_ON)) {
            gBlendShadow.active = 0;
            if (gScreen.bg3.control == SHADOW_BG3_CONTROL) {
                // never leave the shadows up without the blend: they would show as solid paint
                gScreen.lcd.displayControl &= ~DISPCNT_BG3_ON;
            }
            return;
        }
    } else {
        if ((gScreen.lcd.displayControl & DISPCNT_BG3_ON) || gScreen.controls.layerFXControl != 0) {
            return;
        }
    }

    set = &gBlendShadowSets[r->set];
    if (gBlendShadow.loaded != set || !CharsPresent(set)) {
        LoadChars(set);
        gBlendShadow.loaded = set;
        gBlendShadow.cellX = 0x7fff; // video memory was disturbed: redraw the map too
    }

    sx = gRoomControls.scroll_x - gRoomControls.origin_x + gRoomControls.aff_x;
    sy = gRoomControls.scroll_y - gRoomControls.origin_y + gRoomControls.aff_y;
    cx = sx >> 3;
    cy = sy >> 3;
    // redraw on a new cell, and now and then in case a menu borrowed the map buffer
    if (!gBlendShadow.active || cx != gBlendShadow.cellX || cy != gBlendShadow.cellY || (gMain.ticks & 15) == 0) {
        Draw(r, set, cx, cy);
        gBlendShadow.cellX = cx;
        gBlendShadow.cellY = cy;
    }
    gScreen.bg3.subTileMap = gBG3Buffer;
    gScreen.bg3.control = SHADOW_BG3_CONTROL;
    gScreen.bg3.xOffset = sx & 0x1ff;
    gScreen.bg3.yOffset = sy & 0x1ff;
    gScreen.controls.layerFXControl = SHADOW_FX_CONTROL;
    gScreen.controls.alphaBlend = SHADOW_ALPHA;
    gScreen.lcd.displayControl |= DISPCNT_BG3_ON;
    gBlendShadow.active = 1;
}
