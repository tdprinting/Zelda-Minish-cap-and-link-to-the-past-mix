/**
 * @file blend.c
 *
 * @brief Systems for the Minish Cap x A Link to the Past blend.
 *
 * - Dark World: a second layer of every overworld area, drawn by restyling the
 *   existing Minish Cap palettes as they are uploaded to palette RAM.
 *   Link crosses between worlds with the Magic Mirror (hold L + R).
 * - Co-op: publishes this player's state to gBlendLink so the PC host can
 *   mirror it into the other player's game.
 * - Dev hooks used by the automated test harness.
 */
#include "blend.h"
#include "area.h"
#include "common.h"
#include "entity.h"
#include "fade.h"
#include "game.h"
#include "main.h"
#include "message.h"
#include "player.h"
#include "room.h"
#include "save.h"
#include "sound.h"
#include "enemy.h"
#include "color.h"
#include "object.h"

#define MIRROR_HOLD_FRAMES 20

u32 FindFreeObjPalette(u32 paletteCount);
void SetEntityObjPalette(Entity* entity, s32 palette);
/** Uploads the gfx of a sprite frame into the entity's swap gfx slots (asm). */
void sub_080042D0(Entity* entity, u32 frameIndex, u32 spriteIndex);

extern u32 gUsedPalettes;

bool32 Blend_IsDarkWorldActive(void) {
    return BLEND_DARK_WORLD && (gArea.areaMetadata & AR_IS_OVERWORLD);
}

/** Link's and the partner's OBJ palettes keep their colors in the Dark World. */
static bool32 IsPaletteExempt(u32 palIdx) {
    return palIdx == 16u + gPlayerEntity.base.palette.b.b0 ||
           (gBlendLink.partnerPalette != 0 && palIdx == 16u + gBlendLink.partnerPalette);
}

/**
 * BG palette 15 is shared by world tiles and the text box. Its last four
 * colors are the text box grays, white and black, which stay as they are.
 */
static u32 ColorsToTransform(u32 palIdx) {
    return palIdx == 15 ? 12 : 16;
}

/**
 * Restyle one 16 color palette into its Dark World version, in the spirit of
 * A Link to the Past: greens turn olive and brown, skies and water turn
 * violet, and everything loses some light.
 */
void Blend_TransformPalette(u16* pal, u32 palIdx) {
    u32 i, count;
    if (IsPaletteExempt(palIdx)) {
        return;
    }
    count = ColorsToTransform(palIdx);
    for (i = 0; i < count; i++) {
        u32 c = pal[i];
        s32 r = c & 0x1F;
        s32 g = (c >> 5) & 0x1F;
        s32 b = (c >> 10) & 0x1F;
        // weights are in 1/256ths: greens go olive, light drops, shadows go violet
        s32 nr = (r * 141 + g * 64 + b * 26 + 256) >> 8;
        s32 ng = (r * 31 + g * 154 + b * 20) >> 8;
        s32 nb = (r * 38 + g * 38 + b * 102 + 768) >> 8;
        if (nr > 31)
            nr = 31;
        if (nb > 31)
            nb = 31;
        pal[i] = nr | (ng << 5) | (nb << 10);
    }
}

static void RefreshAllPalettes(void) {
    gUsedPalettes = 0xffffffff;
}

static bool32 CanUseMirror(void) {
    if (!(gArea.areaMetadata & AR_IS_OVERWORLD))
        return FALSE;
    if (gMessage.state & MESSAGE_ACTIVE)
        return FALSE;
    if (gPlayerState.framestate != PL_STATE_IDLE && gPlayerState.framestate != PL_STATE_WALK)
        return FALSE;
    return TRUE;
}

static void Warp(u32 area, u32 room, u32 x, u32 y, u32 layer) {
    Transition t;
    MemClear(&t, sizeof(t));
    t.warp_type = 0;
    t.endX = x;
    t.endY = y;
    t.area = area;
    t.room = room;
    t.layer = layer;
    t.facing_direction = gPlayerEntity.base.animationState >> 1;
    t.transitionSFX = SFX_NONE;
    DoExitTransition(&t);
}

/** Reloads the current room at Link's position, so the room comes back in the other world. */
static void WarpInPlace(void) {
    Entity* p = &gPlayerEntity.base;
    Warp(gRoomControls.area, gRoomControls.room, p->x.HALF.HI - gRoomControls.origin_x,
         p->y.HALF.HI - gRoomControls.origin_y, p->collisionLayer);
}

/** Dark World versions of overworld enemies, using enemies Minish Cap already has. */
static u32 DarkWorldEnemy(u32 id) {
    switch (id) {
        case OCTOROK:
            return ROPE;
        case CHUCHU:
            return SPINY_CHUCHU;
        case BEETLE:
            return SPIKED_BEETLE;
        default:
            return id;
    }
}

void Blend_ApplyDarkWorldEnemy(Entity* entity) {
    u32 id;
    if (!Blend_IsDarkWorldActive()) {
        return;
    }
    id = DarkWorldEnemy(entity->id);
    if (id != entity->id) {
        entity->id = id;
        entity->type = 0; // the replacement's default form
    }
}

static void UpdateMirror(void) {
    if ((gInput.heldKeys & (L_BUTTON | R_BUTTON)) == (L_BUTTON | R_BUTTON) && CanUseMirror()) {
        if (++gBlendLink.mirrorTimer == MIRROR_HOLD_FRAMES) {
            BLEND_DARK_WORLD ^= 1;
            SoundReq(SFX_TELEPORTER);
            WarpInPlace();
        }
    } else {
        gBlendLink.mirrorTimer = 0;
    }
}

static void UpdateDevHooks(void) {
    if (gBlendLink.devSetDarkWorld) {
        BLEND_DARK_WORLD = gBlendLink.devSetDarkWorld - 1;
        gBlendLink.devSetDarkWorld = 0;
        RefreshAllPalettes();
    }
    if (gBlendLink.devSpawnEnemy) {
        Entity* e = CreateEnemy(gBlendLink.devSpawnEnemy - 1, 0);
        gBlendLink.devSpawnEnemy = 0;
        if (e != NULL) {
            Blend_ApplyDarkWorldEnemy(e);
            e->x.HALF.HI = gPlayerEntity.base.x.HALF.HI + 32;
            e->y.HALF.HI = gPlayerEntity.base.y.HALF.HI;
            e->collisionLayer = gPlayerEntity.base.collisionLayer;
        }
    }
    if (gBlendLink.devWarp) {
        gBlendLink.devWarp = 0;
        Warp(gBlendLink.devWarpArea, gBlendLink.devWarpRoom, gBlendLink.devWarpX, gBlendLink.devWarpY, 1);
    }
}

static void PublishSelf(void) {
    BlendPlayerSync* s = &gBlendLink.self;
    Entity* p = &gPlayerEntity.base;
    s->x = p->x.HALF.HI;
    s->y = p->y.HALF.HI;
    s->z = p->z.HALF.HI;
    s->area = gRoomControls.area;
    s->room = gRoomControls.room;
    s->animationState = p->animationState;
    s->darkWorld = BLEND_DARK_WORLD;
    s->spriteIndex = p->spriteIndex;
    s->frameIndex = p->frameIndex;
    s->health = gSave.stats.health;
    s->attack = gPlayerState.framestate == PL_STATE_SWORD;
    s->flipX = p->spriteSettings.flipX;
    s->frameSpriteSettings = p->frameSpriteSettings;
    s->collisionLayer = p->collisionLayer;
    s->active = p->spriteSettings.draw != 0;
}

/**
 * Give the partner its own copy of Link's palette with the tunic turned red,
 * like the Red Mail from A Link to the Past.
 */
static void LoadPartnerPalette(Entity* this) {
    u32 slot = FindFreeObjPalette(1);
    u16* src = &gPaletteBuffer[(16 + gPlayerEntity.base.palette.b.b0) * 16];
    u16* dst;
    u32 i;
    if (slot >= 16) {
        return;
    }
    gPaletteList[slot].objPaletteId = 0x3f0;
    gPaletteList[slot]._0_4 = 1;
    gPaletteList[slot]._0_0 = 3;
    gPaletteList[slot]._1 = 0;
    dst = &gPaletteBuffer[(16 + slot) * 16];
    for (i = 0; i < 16; i++) {
        u32 c = src[i];
        u32 r = c & 0x1F;
        u32 g = (c >> 5) & 0x1F;
        u32 b = (c >> 10) & 0x1F;
        if (g > r + 2 && g > b + 2) {
            // green tunic shades become red
            u32 t = r;
            r = g;
            g = t;
        }
        dst[i] = r | (g << 5) | (b << 10);
    }
    USE_PALETTE(16 + slot);
    SetEntityObjPalette(this, slot);
    gBlendLink.partnerPalette = slot;
}

static bool32 PartnerVisibleHere(void) {
    BlendPlayerSync* p = &gBlendLink.partner;
    return gBlendLink.hostConnected && p->active && p->area == gRoomControls.area && p->room == gRoomControls.room &&
           p->darkWorld == BLEND_DARK_WORLD;
}

/** Object function for the co-op partner: a second Link driven by the other player's game. */
void LinkPartner(Entity* this) {
    BlendPlayerSync* p = &gBlendLink.partner;
    if (this->action == 0) {
        this->action = 1;
        this->spriteSettings.draw = 0;
        LoadPartnerPalette(this);
    }
    if (!PartnerVisibleHere()) {
        this->spriteSettings.draw = 0;
        return;
    }
    this->spriteSettings.draw = 1;
    this->x.HALF.HI = p->x;
    this->y.HALF.HI = p->y;
    this->z.HALF.HI = p->z;
    this->collisionLayer = p->collisionLayer;
    this->spriteIndex = p->spriteIndex;
    this->frameIndex = p->frameIndex;
    if (this->frameIndex != this->lastFrameIndex) {
        this->lastFrameIndex = this->frameIndex;
        sub_080042D0(this, this->frameIndex, this->spriteIndex);
    }
    this->frameSpriteSettings = p->frameSpriteSettings;
    this->animationState = p->animationState;
    this->spriteSettings.flipX = p->flipX;
    UpdateSpriteForCollisionLayer(this);
}

static void UpdatePartner(void) {
    Entity* e = gBlendLink.partnerEntity;
    bool32 alive = e != NULL && e->next != NULL && e->kind == OBJECT && e->id == LINK_PARTNER;
    if (!gBlendLink.hostConnected) {
        return;
    }
    if (!alive) {
        gBlendLink.partnerEntity = NULL;
        gBlendLink.partnerPalette = 0;
        e = CreateObject(LINK_PARTNER, 0, 0);
        if (e != NULL) {
            gBlendLink.partnerEntity = e;
        }
    }
}

void Blend_GameUpdate(void) {
    if (gBlendLink.magic != BLEND_MAGIC) {
        MemClear(&gBlendLink, sizeof(gBlendLink));
        gBlendLink.magic = BLEND_MAGIC;
    }
    UpdateDevHooks();
    UpdateMirror();
    PublishSelf();
    UpdatePartner();
}
