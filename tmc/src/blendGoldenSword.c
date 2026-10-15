/**
 * @file blendGoldenSword.c
 *
 * @brief The Golden Sword: the Temple of Bonds' reward and the strongest sword in the game.
 *
 * Found in the co-op temple's big chest. Whatever sword Link carries becomes golden:
 * - it strikes as the Four Sword does, and twice as hard (CalculateDamage);
 * - its blade, its beams and its spin are gilded with a palette that shimmers, made from
 *   the colors each sprite already has (every shade turns to gold of the same light);
 * - a trail of golden sparks follows the blade, and every hit bursts in golden stars.
 * It also teaches the Sword Beam (the temple's old reward), as A Link to the Past's
 * strongest swords shoot beams at full health.
 */
#include "blend.h"
#include "asm.h"
#include "color.h"
#include "common.h"
#include "effects.h"
#include "entity.h"
#include "fade.h"
#include "item.h"
#include "main.h"
#include "object.h"
#include "player.h"
#include "playeritem.h"
#include "room.h"
#include "sound.h"

u32 FindFreeObjPalette(u32 paletteCount);
void SetEntityObjPalette(Entity* entity, s32 palette);
s32 FindPalette(u32 objPaletteId);
void CleanUpObjPalettes(void);
void UpdatePlayerSkills(void);
extern u16 gPaletteBuffer[];

/** Palette ids of the golden copies (Minish Cap's own ids stop well below). */
#define GOLD_PALETTE_ID 0x3f2
#define GOLD_PALETTES 3
#define GOLD_SPARKS 6

typedef struct {
    u16 srcId[GOLD_PALETTES]; /**< palette id each golden copy is made from, 0xffff: unused */
    u8 slot[GOLD_PALETTES];   /**< its OBJ palette slot */
    u8 sparkTimer;
    u8 lastDamage; /**< the last sword strike's damage (for tests) */
    u8 shimmer;
    u8 pad;
    Entity* sparks[GOLD_SPARKS]; /**< golden effects made here, gilded as they appear */
    u8 heldGilt;                 /**< the item picture's palette is gilded */
    u8 pad2[3];
    u16 heldPalette[16];         /**< its own colors, to put back */
} GoldenSwordState;

/**
 * The pictures of items held up are drawn with fixed palettes rather than their entity's
 * (Minish Cap's item art picks its palette per piece), so while Link holds the Golden
 * Sword up, the fixed palette of its blade turns gold for the length of the fanfare.
 */
#define HELD_BLADE_PALETTE 4

extern GoldenSwordState gBlendGoldenSword;

bool32 Blend_HasGoldenSword(void) {
    return GetInventoryValue(ITEM_GOLDEN_SWORD) != 0;
}

/** The sword entities: the blade itself, the dash and the beams. */
static bool32 IsSwordEntity(Entity* e) {
    if (e->kind != PLAYER_ITEM) {
        return FALSE;
    }
    switch (e->id) {
        case PLAYER_ITEM_SWORD:
        case PLAYER_ITEM_DASH_SWORD:
        case PLAYER_ITEM_SWORD_BEAM1:
        case PLAYER_ITEM_SWORD_BEAM2:
        case PLAYER_ITEM_SPIRAL_BEAM:
            return TRUE;
    }
    return FALSE;
}

/** Twice as hard as the Four Sword (the sword strikes with the Four Sword's moves). */
s32 Blend_GoldenSwordDamage(Entity* item, s32 damage) {
    if (IsSwordEntity(item)) {
        if (Blend_HasGoldenSword()) {
            damage *= 2;
        }
        gBlendGoldenSword.lastDamage = damage;
    }
    return damage;
}

/** The sword level the blade strikes with: the Four Sword's when golden. */
u32 Blend_SwordLevel(u32 level) {
    return Blend_HasGoldenSword() ? ITEM_FOURSWORD : level;
}

static u32 SrcId(u32 slot) {
    return slot < 6 ? slot : gPaletteList[slot].objPaletteId;
}

static bool32 IsGoldSlot(u32 slot) {
    u32 i;
    for (i = 0; i < GOLD_PALETTES; i++) {
        if (gBlendGoldenSword.srcId[i] != 0xffff && gBlendGoldenSword.slot[i] == slot) {
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * Gilds one palette: each shade becomes gold of the same brightness, deep amber in the
 * shadows and pale yellow in the light, and a glint runs along the shades over time.
 */
static void Gild(u16* dst, const u16* src, u32 phase) {
    u32 i;
    dst[0] = src[0];
    for (i = 1; i < 16; i++) {
        u32 c = src[i];
        u32 lum = (((c & 0x1F) * 3 + ((c >> 5) & 0x1F) * 6 + ((c >> 10) & 0x1F)) * 205) >> 11;
        u32 r, g, b;
        if (((i * 5 + phase) & 15) < 2) {
            lum += 6; // the glint
        }
        r = lum + 7;
        g = ((lum * 13) >> 4) + 3;
        b = (lum * lum) >> 6;
        if (r > 31)
            r = 31;
        if (g > 31)
            g = 31;
        if (b > 24)
            b = 24;
        dst[i] = r | (g << 5) | (b << 10);
    }
}

/** The golden copy of palette slot src, made if there is none yet. Returns its slot, or -1. */
static s32 GoldSlotFor(u32 src) {
    u32 id = SrcId(src);
    u32 i, free = GOLD_PALETTES;
    s32 slot;
    for (i = 0; i < GOLD_PALETTES; i++) {
        if (gBlendGoldenSword.srcId[i] == id) {
            slot = gBlendGoldenSword.slot[i];
            if (gPaletteList[slot].objPaletteId == GOLD_PALETTE_ID + i) {
                return slot;
            }
            gBlendGoldenSword.srcId[i] = 0xffff; // its slot was taken back
        }
        if (gBlendGoldenSword.srcId[i] == 0xffff && free == GOLD_PALETTES) {
            free = i;
        }
    }
    if (free == GOLD_PALETTES) {
        return -1;
    }
    slot = FindPalette(GOLD_PALETTE_ID + free);
    if (slot < 0) {
        slot = FindFreeObjPalette(1);
        if (slot < 0 || slot >= 16) {
            CleanUpObjPalettes();
            slot = FindFreeObjPalette(1);
            if (slot < 0 || slot >= 16) {
                return -1;
            }
        }
    }
    gPaletteList[slot].objPaletteId = GOLD_PALETTE_ID + free;
    gPaletteList[slot]._0_4 = 1;
    gPaletteList[slot]._0_0 = 3;
    gPaletteList[slot]._1 = 0;
    gBlendGoldenSword.srcId[free] = id;
    gBlendGoldenSword.slot[free] = slot;
    Gild(&gPaletteBuffer[(16 + slot) * 16], &gPaletteBuffer[(16 + src) * 16], gBlendGoldenSword.shimmer);
    USE_PALETTE(16 + slot);
    return slot;
}

static void Gilt(Entity* e) {
    s32 slot;
    if (e == NULL || IsGoldSlot(e->palette.b.b0)) {
        return;
    }
    slot = GoldSlotFor(e->palette.b.b0);
    if (slot >= 0) {
        SetEntityObjPalette(e, slot);
    }
}

void Blend_GoldenSwordGild(Entity* e) {
    if (Blend_HasGoldenSword()) {
        Gilt(e);
    }
}

/** Keeps every golden copy shimmering, and frees the ones nothing wears any more. */
static void Shimmer(void) {
    u32 i;
    gBlendGoldenSword.shimmer++;
    for (i = 0; i < GOLD_PALETTES; i++) {
        u32 slot = gBlendGoldenSword.slot[i];
        s32 src;
        if (gBlendGoldenSword.srcId[i] == 0xffff) {
            continue;
        }
        if (gPaletteList[slot].objPaletteId != GOLD_PALETTE_ID + i || gPaletteList[slot]._0_0 != 3) {
            gBlendGoldenSword.srcId[i] = 0xffff;
            continue;
        }
        src = FindPalette(gBlendGoldenSword.srcId[i]);
        if (src < 0 || (gBlendGoldenSword.shimmer & 3) != 0) {
            continue;
        }
        Gild(&gPaletteBuffer[(16 + slot) * 16], &gPaletteBuffer[(16 + src) * 16], gBlendGoldenSword.shimmer >> 2);
        USE_PALETTE(16 + slot);
    }
}

static void Spark(Entity* at, s32 dx, s32 dy, u32 fx) {
    u32 i;
    Entity* e;
    for (i = 0; i < GOLD_SPARKS; i++) {
        if (gBlendGoldenSword.sparks[i] == NULL) {
            break;
        }
    }
    if (i == GOLD_SPARKS) {
        return;
    }
    e = CreateFx(at, fx, 0);
    if (e == NULL) {
        return;
    }
    e->x.HALF.HI += dx;
    e->y.HALF.HI += dy;
    e->z.HALF.HI = at->z.HALF.HI;
    gBlendGoldenSword.sparks[i] = e;
}

/** Golden stars burst from a foe the golden sword strikes. */
void Blend_GoldenSwordHit(Entity* item, Entity* foe) {
    if (!IsSwordEntity(item) || !Blend_HasGoldenSword()) {
        return;
    }
    Spark(foe, 0, -4, FX_STARS_STRIKE);
}

static s32 Jitter(u32 range) {
    // no division routine is linked in, so scale a random byte instead
    return (s32)(((Random() & 0xff) * (range * 2 + 1)) >> 8) - (s32)range;
}

/** A trail of golden sparks along the blade while it swings, spins or flies. */
static void Trail(Entity* e) {
    s32 dx = 0, dy = 0, w = 6, h = 6;
    if (e->hitbox != NULL && (e->flags & ENT_COLLIDE)) {
        dx = e->hitbox->offset_x;
        dy = e->hitbox->offset_y;
        w = e->hitbox->width;
        h = e->hitbox->height;
    } else if (e->id == PLAYER_ITEM_SWORD && e->frameIndex == 0xff) {
        return; // the blade is sheathed
    }
    Spark(e, dx + Jitter(w), dy + Jitter(h), (Random() & 3) ? FX_SPARKLE2 : FX_SPARKLE4);
}

static void GildHeldPicture(bool32 held) {
    u16* pal = &gPaletteBuffer[(16 + HELD_BLADE_PALETTE) * 16];
    u32 i;
    if (held) {
        if (!gBlendGoldenSword.heldGilt) {
            for (i = 0; i < 16; i++) {
                gBlendGoldenSword.heldPalette[i] = pal[i];
            }
            gBlendGoldenSword.heldGilt = TRUE;
        }
        Gild(pal, gBlendGoldenSword.heldPalette, gBlendGoldenSword.shimmer >> 2);
        USE_PALETTE(16 + HELD_BLADE_PALETTE);
    } else if (gBlendGoldenSword.heldGilt) {
        for (i = 0; i < 16; i++) {
            pal[i] = gBlendGoldenSword.heldPalette[i];
        }
        USE_PALETTE(16 + HELD_BLADE_PALETTE);
        gBlendGoldenSword.heldGilt = FALSE;
    }
}

/** The temple's old reward comes with the sword. */
static void TeachBeam(void) {
    if (GetInventoryValue(ITEM_SKILL_SWORD_BEAM) == 0) {
        SetInventoryValue(ITEM_SKILL_SWORD_BEAM, 1);
        UpdatePlayerSkills();
    }
}

void Blend_GoldenSwordUpdate(void) {
    u32 i, list;
    bool32 golden = Blend_HasGoldenSword();
    bool32 trail = FALSE;
    bool32 held = FALSE;
    Entity* e;

    if (gBlendGoldenSword.pad != 0x5a) {
        // first run since power on
        for (i = 0; i < GOLD_PALETTES; i++) {
            gBlendGoldenSword.srcId[i] = 0xffff;
        }
        for (i = 0; i < GOLD_SPARKS; i++) {
            gBlendGoldenSword.sparks[i] = NULL;
        }
        gBlendGoldenSword.heldGilt = FALSE;
        gBlendGoldenSword.pad = 0x5a;
    }
    // forget sparks that are gone, gild the new ones
    for (i = 0; i < GOLD_SPARKS; i++) {
        e = gBlendGoldenSword.sparks[i];
        if (e == NULL) {
            continue;
        }
        if (e->next == NULL || e->kind != OBJECT || e->id != SPECIAL_FX) {
            gBlendGoldenSword.sparks[i] = NULL;
        } else {
            Gilt(e);
        }
    }
    Shimmer();

    for (list = 0; list < ARRAY_COUNT(gEntityLists); list++) {
        LinkedList* l = &gEntityLists[list];
        for (e = l->first; (u32)e != (u32)l; e = e->next) {
            if (e->kind == OBJECT && (e->id == LINK_HOLDING_ITEM || e->id == GROUND_ITEM) &&
                e->type == ITEM_GOLDEN_SWORD) {
                // the picture held up from the chest
                held = TRUE;
                if ((gBlendGoldenSword.shimmer & 7) == 0) {
                    Spark(e, Jitter(8), Jitter(6) - (e->id == LINK_HOLDING_ITEM ? 22 : 4), FX_SPARKLE2);
                }
            } else if (golden && IsSwordEntity(e)) {
                Gilt(e);
                if (e->id != PLAYER_ITEM_SWORD || e->frameIndex != 0xff || (e->flags & ENT_COLLIDE)) {
                    if (!trail && gBlendGoldenSword.sparkTimer == 0) {
                        Trail(e);
                        trail = TRUE;
                    }
                }
            }
        }
    }
    GildHeldPicture(held);
    if (trail) {
        gBlendGoldenSword.sparkTimer = 2;
    } else if (gBlendGoldenSword.sparkTimer != 0) {
        gBlendGoldenSword.sparkTimer--;
    }
    if (golden) {
        TeachBeam();
    }
}
