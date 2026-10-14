#ifndef BLEND_H
#define BLEND_H

#include "global.h"

/**
 * Shared state for the Minish Cap x A Link to the Past blend.
 * Lives at a fixed EWRAM address (0x0203F000) so the PC host can read and
 * write it for split-screen co-op and automated tests.
 */
typedef struct {
    /*0x00*/ u16 x;
    /*0x02*/ u16 y;
    /*0x04*/ u8 area;
    /*0x05*/ u8 room;
    /*0x06*/ u8 animationState;
    /*0x07*/ u8 darkWorld;
    /*0x08*/ u16 spriteIndex;
    /*0x0a*/ u8 frameIndex;
    /*0x0b*/ u8 active; /**< nonzero when this player is in normal gameplay */
    /*0x0c*/ s8 z;
    /*0x0d*/ u8 health;
    /*0x0e*/ u8 attack; /**< nonzero on frames this player swings the sword */
    /*0x0f*/ u8 flipX;
    /*0x10*/ u8 frameSpriteSettings;
    /*0x11*/ u8 collisionLayer;
    /*0x12*/ u16 pad;
} BlendPlayerSync;

typedef struct {
    /*0x00*/ u32 magic;         /**< BLEND_MAGIC once initialized */
    /*0x04*/ u8 hostConnected;  /**< set by the PC host when co-op is running */
    /*0x05*/ u8 playerIndex;    /**< 0 = player 1, 1 = player 2 (set by host) */
    /*0x06*/ u8 mirrorTimer;
    /*0x07*/ u8 devWarp;        /**< host test hook: nonzero requests a warp */
    /*0x08*/ u8 devWarpArea;
    /*0x09*/ u8 devWarpRoom;
    /*0x0a*/ u16 devWarpX;
    /*0x0c*/ u16 devWarpY;
    /*0x0e*/ u8 devSetDarkWorld; /**< host test hook: 1 = light, 2 = dark */
    /*0x0f*/ u8 partnerPalette;  /**< OBJ palette slot used by the partner Link */
    /*0x10*/ BlendPlayerSync self;    /**< written by the game every frame */
    /*0x24*/ BlendPlayerSync partner; /**< written by the host every frame */
    /*0x38*/ struct Entity_* partnerEntity;
    /*0x3c*/ u8 devSpawnEnemy; /**< host test hook: enemy id + 1 to spawn next to Link */
} BlendLink;

#define BLEND_MAGIC 0x444E4C42 /* "BLND" */

extern BlendLink gBlendLink;

/** Dark World flag, stored in an unused byte of the save file. */
#define BLEND_DARK_WORLD (gSave.fillerA[0])

bool32 Blend_IsDarkWorldActive(void);
void Blend_GameUpdate(void);
void Blend_ApplyDarkWorldEnemy(struct Entity_* entity);
void Blend_TransformPalette(u16* pal, u32 palIdx);
void LinkPartner(struct Entity_* this);

#endif // BLEND_H
