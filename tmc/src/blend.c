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

#define MIRROR_HOLD_FRAMES 20

extern u32 gUsedPalettes;

bool32 Blend_IsDarkWorldActive(void) {
    return BLEND_DARK_WORLD && (gArea.areaMetadata & AR_IS_OVERWORLD);
}

/** OBJ palette 0 (Link) keeps its colors in the Dark World. */
static bool32 IsPaletteExempt(u32 palIdx) {
    return palIdx == 16;
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

static void UpdateMirror(void) {
    if ((gInput.heldKeys & (L_BUTTON | R_BUTTON)) == (L_BUTTON | R_BUTTON) && CanUseMirror()) {
        if (++gBlendLink.mirrorTimer == MIRROR_HOLD_FRAMES) {
            BLEND_DARK_WORLD ^= 1;
            SoundReq(SFX_TELEPORTER);
            RefreshAllPalettes();
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
    if (gBlendLink.devWarp) {
        Transition t;
        MemClear(&t, sizeof(t));
        t.warp_type = 0;
        t.endX = gBlendLink.devWarpX;
        t.endY = gBlendLink.devWarpY;
        t.area = gBlendLink.devWarpArea;
        t.room = gBlendLink.devWarpRoom;
        t.layer = 1;
        t.facing_direction = 4;
        t.transitionSFX = SFX_NONE;
        gBlendLink.devWarp = 0;
        DoExitTransition(&t);
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
    s->active = 1;
}

void Blend_GameUpdate(void) {
    if (gBlendLink.magic != BLEND_MAGIC) {
        MemClear(&gBlendLink, sizeof(gBlendLink));
        gBlendLink.magic = BLEND_MAGIC;
    }
    UpdateDevHooks();
    UpdateMirror();
    PublishSelf();
}
