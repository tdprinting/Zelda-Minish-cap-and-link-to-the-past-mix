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
    /*0x12*/ u16 itemSpriteIndex; /**< held item (sword etc.), 0 when none is drawn */
    /*0x14*/ s16 itemX;
    /*0x16*/ s16 itemY;
    /*0x18*/ u8 itemFrameIndex;
    /*0x19*/ u8 itemFlipX;
    /*0x1a*/ u8 itemPalette;
    /*0x1b*/ u8 itemFrameSpriteSettings;
    /*0x1c*/ u8 itemHurtType; /**< nonzero while the item can hit things */
    /*0x1d*/ u8 itemDamage;
    /*0x1e*/ s8 itemHitboxX;
    /*0x1f*/ s8 itemHitboxY;
    /*0x20*/ u8 itemHitboxWidth;
    /*0x21*/ u8 itemHitboxHeight;
    /*0x22*/ u16 pad;
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
    /*0x34*/ BlendPlayerSync partner; /**< written by the host every frame */
    /*0x58*/ struct Entity_* partnerEntity;
    /*0x5c*/ struct Entity_* partnerItemEntity;
    /*0x60*/ u8 devSpawnEnemy; /**< host test hook: enemy id + 1 to spawn next to Link */
    /*0x61*/ u8 devGiveItem;   /**< host test hook: item id to give and equip on B */
    /*0x62*/ u8 devItemGet;    /**< host test hook: item id to award with the item-get cutscene */
    /*0x63*/ u8 medallionUsed; /**< a medallion was cast in this room */
    /*0x64*/ u16 roomKey;      /**< area << 8 | room of the room Link was last seen in */
    /*0x66*/ u8 treasureSpawned;
    /*0x67*/ u8 bunny;         /**< Link is a bunny (Dark World without the Moon Pearl) */
    /*0x68*/ u8 awardCooldown;
} BlendLink;

#define BLEND_MAGIC 0x444E4C42 /* "BLND" */

extern BlendLink gBlendLink;

/** Dark World flag, stored in an unused byte of the save file. */
#define BLEND_DARK_WORLD (gSave.fillerA[0])
/** The medallion cast with R + Select, chosen with L + Select. */
#define BLEND_MEDALLION (gSave.fillerA[1])

/* A Link to the Past items, stored in Minish Cap item ids the game never used. */
#define ITEM_MAGIC_MIRROR ITEM_32
#define ITEM_MOON_PEARL ITEM_33
#define ITEM_QUAKE ITEM_ORB_GREEN
#define ITEM_ETHER ITEM_ORB_BLUE
#define ITEM_BOMBOS ITEM_ORB_RED
#define ITEM_ICE_ROD ITEM_UNUSED_SWORD

bool32 Blend_IsDarkWorldActive(void);
void Blend_GameUpdate(void);
void Blend_ApplyDarkWorldEnemy(struct Entity_* entity);
void Blend_TransformPalette(u16* pal, u32 palIdx);
void LinkPartner(struct Entity_* this);
u32 Blend_ItemSpriteAnim(u32 item);

#endif // BLEND_H
