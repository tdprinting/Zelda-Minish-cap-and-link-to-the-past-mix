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
    /*0x69*/ u8 hookState;    /**< HOOK_* */
    /*0x6a*/ u16 byrnaTimer;  /**< frames the Cane of Byrna's ring stays up */
    /*0x6c*/ struct Entity_* hook;
    /*0x70*/ u8 somariaUsed;  /**< a Somaria block was made in this room */
    /*0x71*/ u8 devHudIcon;   /**< test hook: show this item's icon on the B button */
    /*0x72*/ u16 heldKeys;    /**< the real buttons this frame, before the blend hides any from the game */
    /*0x74*/ u16 newKeys;     /**< buttons newly pressed this frame, by the same measure */
    /*0x76*/ u8 freezeTimer[4];   /**< frames each Ice Rod-frozen enemy stays frozen */
    /*0x7a*/ u8 freezePalette[4]; /**< the palette each frozen enemy had */
    /*0x7e*/ u16 pad7e;
    /*0x80*/ struct Entity_* frozen[4];
    /*0x90*/ u8 icePalette[4];    /**< OBJ palette slot holding each frozen enemy's icy colors */
    /*0x94*/ u8 bossState;        /**< BOSS_*: the A Link to the Past boss fight in this room */
    /*0x95*/ u8 bossMaxHealth;    /**< Link's max health when the Heart Container appeared */
    /*0x96*/ u8 endingTimer;      /**< frames since Ganon fell (the ending, then the credits) */
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

/**
 * Link's animations for the A Link to the Past items, in sprite 159 (made by
 * blend/anim/build.py; keep the order of its GROUPS). Each is a group of four:
 * up, right, down, left.
 */
#define ANIM_BLEND_HAMMER 0x9f00
#define ANIM_BLEND_HOOKSHOT 0x9f04
#define ANIM_BLEND_FIRE_ROD 0x9f08
#define ANIM_BLEND_ICE_ROD 0x9f0c
#define ANIM_BLEND_SOMARIA 0x9f10
#define ANIM_BLEND_BYRNA 0x9f14
#define ANIM_BLEND_POWDER 0x9f18
#define ANIM_BLEND_BOMBOS 0x9f1c
#define ANIM_BLEND_ETHER 0x9f20
#define ANIM_BLEND_QUAKE 0x9f24
#define ANIM_BLEND_MIRROR 0x9f28

/** Which new items are switched in for their Minish Cap partner (one bit per item, from ITEM_HOOKSHOT). */
#define BLEND_VARIANTS_ON (gSave.fillerA[3])

bool32 Blend_IsDarkWorldActive(void);
struct Transition;
bool32 Blend_IsTransitionClosed(const struct Transition* t);
bool32 Blend_IsCaveSealed(u32 flag);
void Blend_GameUpdate(void);
void Blend_ApplyDarkWorldEnemy(struct Entity_* entity);
void Blend_TransformPalette(u16* pal, u32 palIdx);
void LinkPartner(struct Entity_* this);
u32 Blend_ItemSpriteAnim(u32 item);
/** The item whose button icon the HUD shows for this equipped item. */
u32 Blend_HudIcon(u32 item);
void Blend_FreezeEnemy(struct Entity_* enemy);
/** Soft overworld shadows on background 3 (blendShadows.c); runs after the room's managers. */
void Blend_ShadowUpdate(void);
/** After a menu: puts the shadow layer's characters and map back before the room fades in. */
void Blend_ShadowRestore(void);
/** The current room has a shadow layer, so the game's own background 3 overlays stay off. */
bool32 Blend_RoomHasShadows(void);

#endif // BLEND_H
