/**
 * @file blendCoop.c
 *
 * @brief Co-op for the Minish Cap x A Link to the Past blend.
 *
 * Each player plays their own copy of the game (blend/host/coop.c runs both),
 * with their own hearts, items, Rupees and menus. On top of the partner Link
 * of blend.c, this adds:
 * - Going down: a player whose hearts run out lies down instead of getting a
 *   game over (unless a bottled fairy saves them first). The partner revives
 *   them by walking up and pressing R with a Revive Fairy. The game is over
 *   only when both are down, or when the one down holds A + B to give up.
 * - The Revive Fairy, sold in Stockwell's shop, with its own picture.
 * - The host's quick inventory: the game lists the menu here and equips what
 *   the host asks for, while the game keeps running.
 * - Items the partner found, given here quietly (the host shares progress),
 *   and regrouping: a warp to where the partner is.
 */
#include "blendCoop.h"
#include "blend.h"
#include "area.h"
#include "asm.h"
#include "color.h"
#include "common.h"
#include "effects.h"
#include "entity.h"
#include "fade.h"
#include "game.h"
#include "item.h"
#include "itemMetaData.h"
#include "main.h"
#include "message.h"
#include "object.h"
#include "pauseMenu.h"
#include "player.h"
#include "room.h"
#include "save.h"
#include "sound.h"
#include "structures.h"
#include "ui.h"

#include "../custom/gen/coop/revive_fairy.h"

u32 FindFreeObjPalette(u32 paletteCount);
void SetEntityObjPalette(Entity* entity, s32 palette);
void CleanUpObjPalettes(void);
extern u32 gFrameObjLists[];
extern Frame* gSpriteAnimations_322[];
u32 Blend_SlotShareItem(u32 item);
void Blend_SetSlotShare(u32 item, bool32 on);

/** The sprite the item pictures (on the ground, held up, for sale) come from. */
#define ITEM_PICTURE_SPRITE 322
/** Marks the Revive Fairy's palette in gPaletteList. */
#define REVIVE_PALETTE_ID 0x3f1
/** How close Link must stand to a downed partner to revive them. */
#define REVIVE_REACH 28
/** Frames of A + B that give up waiting. */
#define GIVE_UP_FRAMES 120

static void InitBlock(void) {
    if (gBlendCoop.magic != BLEND_COOP_MAGIC || gBlendCoop.version != BLEND_COOP_VERSION) {
        MemClear(&gBlendCoop, sizeof(gBlendCoop));
        gBlendCoop.magic = BLEND_COOP_MAGIC;
        gBlendCoop.version = BLEND_COOP_VERSION;
    }
    gBlendCoop.saveAddr = (u32)&gSave;
    gBlendCoop.mainAddr = (u32)&gMain;
    gBlendCoop.itemFramesAddr = (u32)&gSpritePtrs[ITEM_PICTURE_SPRITE];
    gBlendCoop.frameObjListsAddr = (u32)gFrameObjLists;
}

static bool32 PartnerHere(void) {
    BlendPlayerSync* p = &gBlendLink.partner;
    return gBlendLink.hostConnected && p->area == gRoomControls.area && p->room == gRoomControls.room &&
           p->darkWorld == BLEND_DARK_WORLD;
}

static bool32 LinkIsFree(void) {
    if (gMessage.state & MESSAGE_ACTIVE)
        return FALSE;
    if (gPlayerState.framestate != PL_STATE_IDLE && gPlayerState.framestate != PL_STATE_WALK)
        return FALSE;
    return gPlayerState.controlMode == CONTROL_ENABLED && gSave.stats.health != 0;
}

static Entity* SparkleAt(u32 fx, s32 x, s32 y) {
    Entity* e = CreateFx(&gPlayerEntity.base, fx, 0);
    if (e != NULL) {
        e->x.HALF.HI = x;
        e->y.HALF.HI = y;
        e->z.HALF.HI = -8;
    }
    return e;
}

/* ---- The Revive Fairy's picture --------------------------------------------- */

/** Puts the Revive Fairy's colors in an OBJ palette slot (shared by every picture of it). */
static s32 LoadRevivePalette(void) {
    s32 slot = FindPalette(REVIVE_PALETTE_ID);
    u16* dst;
    u32 i;
    if (slot < 0) {
        slot = FindFreeObjPalette(1);
        if (slot < 0 || slot >= 16) {
            CleanUpObjPalettes();
            slot = FindFreeObjPalette(1);
        }
        if (slot < 0 || slot >= 16) {
            return -1;
        }
        gPaletteList[slot].objPaletteId = REVIVE_PALETTE_ID;
        gPaletteList[slot]._0_4 = 1;
        gPaletteList[slot]._0_0 = 3;
        gPaletteList[slot]._1 = 0;
    }
    dst = &gPaletteBuffer[(16 + slot) * 16];
    for (i = 0; i < 16; i++) {
        dst[i] = sReviveFairyPalette[i];
    }
    USE_PALETTE(16 + slot);
    gBlendCoop.revivePalette = slot + 1;
    return slot;
}

bool32 Blend_CoopKeepsPalette(u32 palIdx) {
    return gBlendCoop.magic == BLEND_COOP_MAGIC && gBlendCoop.revivePalette != 0 &&
           palIdx == 16u + gBlendCoop.revivePalette - 1;
}

void Blend_CoopItemPicture(Entity* this) {
    s32 slot;
    if (this->type != ITEM_REVIVE_FAIRY) {
        return;
    }
    this->spriteIndex = REVIVE_FAIRY_SPRITE;
    this->animIndex = 0xff;
    slot = LoadRevivePalette();
    if (slot >= 0) {
        SetEntityObjPalette(this, slot);
    }
}

u32 Blend_CoopItemAnim(Entity* this) {
    if (this->type == ITEM_REVIVE_FAIRY && this->spriteIndex == REVIVE_FAIRY_SPRITE) {
        return 0;
    }
    return Blend_ItemSpriteAnim(this->type);
}

void Blend_GiveReviveFairy(void) {
    if (BLEND_REVIVE_FAIRIES < REVIVE_FAIRIES_MAX) {
        BLEND_REVIVE_FAIRIES++;
    }
}

/* ---- Going down and being revived ------------------------------------------- */

static u32 ReviveHealth(void) {
    u32 h = (gSave.stats.maxHealth / 2) & ~7;
    if (h < 24) {
        h = 24;
    }
    if (h > gSave.stats.maxHealth) {
        h = gSave.stats.maxHealth;
    }
    return h;
}

s32 Blend_CoopWhileDown(void) {
    InitBlock();
    if (gBlendCoop.downed && gBlendCoop.partnerReviveSeq != gBlendCoop.seenReviveSeq) {
        // the partner used a Revive Fairy on us: its fairy circles Link as he gets up
        gBlendCoop.seenReviveSeq = gBlendCoop.partnerReviveSeq;
        gBlendCoop.downed = 0;
        gBlendCoop.reviveFxTimer = 60;
        CreateObject(FAIRY, 0x60, 2);
        SoundReq(SFX_SECRET_BIG);
        return ReviveHealth();
    }
    if (gBlendCoop.gaveUp || !gBlendLink.hostConnected || !gBlendCoop.partnerInGame || gBlendCoop.partnerDowned) {
        // alone, or both down: the game over
        gBlendCoop.downed = 0;
        return 0;
    }
    if (!gBlendCoop.downed) {
        gBlendCoop.seenReviveSeq = gBlendCoop.partnerReviveSeq;
        gBlendCoop.downed = 1;
        gBlendCoop.giveUpTimer = 0;
    }
    if ((gInput.heldKeys & (A_BUTTON | B_BUTTON)) == (A_BUTTON | B_BUTTON)) {
        if (++gBlendCoop.giveUpTimer >= GIVE_UP_FRAMES) {
            gBlendCoop.gaveUp = 1;
            gBlendCoop.downed = 0;
            return 0;
        }
    } else {
        gBlendCoop.giveUpTimer = 0;
    }
    return -1;
}

bool32 Blend_CoopPartnerDowned(void) {
    return gBlendCoop.magic == BLEND_COOP_MAGIC && gBlendLink.hostConnected && gBlendCoop.partnerDowned;
}

/** Standing next to a downed partner, R uses a Revive Fairy on them. */
static void UpdateRevivePartner(void) {
    BlendPlayerSync* p = &gBlendLink.partner;
    s32 dx, dy;
    gBlendCoop.reviveNear = 0;
    if (gBlendCoop.reviveCooldown != 0) {
        gBlendCoop.reviveCooldown--;
        return;
    }
    if (!Blend_CoopPartnerDowned() || !PartnerHere() || !LinkIsFree()) {
        return;
    }
    dx = (s32)p->x - gPlayerEntity.base.x.HALF.HI;
    dy = (s32)p->y - gPlayerEntity.base.y.HALF.HI;
    if (dx < -REVIVE_REACH || dx > REVIVE_REACH || dy < -REVIVE_REACH || dy > REVIVE_REACH) {
        return;
    }
    gBlendCoop.reviveNear = 1;
    // R belongs to the revive here: Link must not roll or grab
    gInput.heldKeys &= ~R_BUTTON;
    gInput.newKeys &= ~R_BUTTON;
    if (!(gBlendLink.newKeys & R_BUTTON)) {
        return;
    }
    if (BLEND_REVIVE_FAIRIES == 0) {
        SoundReq(SFX_MENU_ERROR);
        return;
    }
    BLEND_REVIVE_FAIRIES--;
    gBlendCoop.reviveSeq++;
    gBlendCoop.reviveCooldown = 90;
    SoundReq(SFX_SECRET_BIG);
    SparkleAt(FX_SPARKLE_SECRET, p->x, p->y);
    SparkleAt(FX_SPARKLE2, p->x - 8, p->y - 4);
    SparkleAt(FX_SPARKLE2, p->x + 8, p->y - 4);
}

/** The marker's frames: the wings beat down and back up. */
static const u8 sWingbeat[] = { 0, 1, 2, 1 };

/** Type 4 of the partner Link object: the Revive Fairy, bobbing over a downed partner. */
void Blend_CoopReviveMarker(Entity* this) {
    BlendPlayerSync* p = &gBlendLink.partner;
    u32 t, frame;
    if (this->action == 0) {
        s32 slot = LoadRevivePalette();
        this->action = 1;
        this->spriteSettings.draw = 0;
        this->spriteSettings.shadow = 0;
        this->flags &= ~ENT_COLLIDE;
        if (slot >= 0) {
            SetEntityObjPalette(this, slot);
        }
    }
    if (!Blend_CoopPartnerDowned() || !PartnerHere()) {
        this->spriteSettings.draw = 0;
        return;
    }
    if (gBlendCoop.revivePalette == 0 || this->palette.b.b0 != gBlendCoop.revivePalette - 1) {
        s32 slot = LoadRevivePalette();
        if (slot >= 0) {
            SetEntityObjPalette(this, slot);
        }
    }
    t = gRoomTransition.frameCount;
    frame = sWingbeat[(t >> 3) & 3];
    this->spriteIndex = REVIVE_FAIRY_SPRITE;
    if (frame != this->frameIndex || this->lastFrameIndex == 0xff) {
        this->frameIndex = frame;
    }
    if (this->frameIndex != this->lastFrameIndex) {
        this->lastFrameIndex = this->frameIndex;
        sub_080042D0(this, this->frameIndex, REVIVE_FAIRY_SPRITE);
    }
    this->spriteSettings.draw = 1;
    this->x.HALF.HI = p->x;
    this->y.HALF.HI = p->y;
    // bob up and down over the partner's head
    this->z.HALF.HI = -26 + (((t >> 4) & 1) ? -1 : 0) + (((t >> 5) & 1) ? -1 : 0);
    this->collisionLayer = p->collisionLayer;
    UpdateSpriteForCollisionLayer(this);
}

/* ---- The host's quick inventory --------------------------------------------- */

static void AddEntry(u32 item, u32 icon, u32 equip, u32 variant) {
    u32 n = gBlendCoop.menuCount;
    const Frame* f;
    if (n >= ARRAY_COUNT(gBlendCoop.menuItem)) {
        return;
    }
    f = gSpriteAnimations_322[icon];
    gBlendCoop.menuItem[n] = item;
    gBlendCoop.menuIcon[n] = icon;
    gBlendCoop.menuFrame[n] = f->index;
    gBlendCoop.menuEquip[n] = equip;
    gBlendCoop.menuVariant[n] = variant;
    gBlendCoop.menuCount = n + 1;
}

static bool32 EntryEquipped(u32 i, u32 slot) {
    u32 equipped = gSave.stats.equipped[slot];
    u32 variant = gBlendCoop.menuVariant[i];
    if (gBlendCoop.menuEquip[i] != equipped) {
        return FALSE;
    }
    if (variant == 0) {
        return TRUE;
    }
    // a shared slot shows the item that is switched on
    if (variant == 0xff) {
        return Blend_ItemVariant(equipped) == ITEM_NONE;
    }
    return Blend_ItemVariant(equipped) == variant;
}

/** The pause menu's items, in its order, with the A Link to the Past items that share their slots. */
static void PublishMenu(void) {
    static const u8 sRods[] = { ITEM_FIRE_ROD, ITEM_ICE_ROD };
    u8 bySlot[MENU_SLOT_SAVE_BUTTON];
    u32 item, slot, i, share;
    MemClear(bySlot, sizeof(bySlot));
    for (item = ITEM_SMITH_SWORD; item < ITEM_BOTTLE_EMPTY; item++) {
        slot = gItemMetaData[item].menuSlot;
        if (GetInventoryValue(item) == 1 && slot < MENU_SLOT_SAVE_BUTTON) {
            bySlot[slot] = item;
        }
    }
    gBlendCoop.menuCount = 0;
    for (slot = 0; slot < MENU_SLOT_SAVE_BUTTON; slot++) {
        item = bySlot[slot];
        if (item == ITEM_NONE) {
            continue;
        }
        if (item == ITEM_LANTERN_OFF || item == ITEM_LANTERN_ON) {
            item = gPauseMenuOptions.unk15 ? gPauseMenuOptions.unk15 : item;
        }
        if (ItemIsBottle(item)) {
            u32 content = gSave.stats.bottles[item - ITEM_BOTTLE1];
            AddEntry(content, content, item, 0);
            continue;
        }
        share = Blend_SlotShareItem(item);
        AddEntry(item, item, item, share != ITEM_NONE ? 0xff : 0);
        if (share != ITEM_NONE) {
            AddEntry(share, Blend_ItemSpriteAnim(share), item, share);
        }
        if (item == ITEM_PACCI_CANE) {
            for (i = 0; i < ARRAY_COUNT(sRods); i++) {
                if (GetInventoryValue(sRods[i])) {
                    AddEntry(sRods[i], Blend_ItemSpriteAnim(sRods[i]), sRods[i], 0);
                }
            }
        }
    }
    gBlendCoop.equippedA = 0;
    gBlendCoop.equippedB = 0;
    for (i = 0; i < gBlendCoop.menuCount; i++) {
        if (gBlendCoop.equippedA == 0 && EntryEquipped(i, SLOT_A)) {
            gBlendCoop.equippedA = i + 1;
        }
        if (gBlendCoop.equippedB == 0 && EntryEquipped(i, SLOT_B)) {
            gBlendCoop.equippedB = i + 1;
        }
    }
}

static void HandleQuickEquip(void) {
    u32 i = gBlendCoop.quickEquip;
    u32 slot = gBlendCoop.quickEquipSlot;
    u32 variant;
    if (i == 0) {
        return;
    }
    gBlendCoop.quickEquip = 0;
    i--;
    if (i >= gBlendCoop.menuCount || slot > SLOT_B || gBlendCoop.menuEquip[i] == ITEM_NONE) {
        return;
    }
    ForceEquipItem(gBlendCoop.menuEquip[i], slot);
    variant = gBlendCoop.menuVariant[i];
    if (variant == 0xff) {
        Blend_SetSlotShare(gBlendCoop.menuEquip[i], FALSE);
    } else if (variant != 0) {
        Blend_SetSlotShare(gBlendCoop.menuEquip[i], TRUE);
    }
    gHUD.unk_13 = 0x7f;
    gHUD.unk_14 = 0x7f;
    SoundReq(SFX_TEXTBOX_SELECT);
}

/* ---- Shared progress and regrouping ----------------------------------------- */

/** An item the partner found: the host shares it, and it arrives without a cutscene. */
static void HandleShareGive(void) {
    u32 item = gBlendCoop.shareGive;
    if (item == ITEM_NONE) {
        return;
    }
    gBlendCoop.shareGive = 0;
    if (GetInventoryValue(item) == 0) {
        GiveItem(item, 0);
        SoundReq(SFX_ITEM_GET);
    }
}

/** Goes to where the partner is: their room, their world, their spot. */
static void HandleRegroup(void) {
    BlendPlayerSync* p = &gBlendLink.partner;
    Transition t;
    if (!gBlendCoop.regroup) {
        return;
    }
    gBlendCoop.regroup = 0;
    if (!LinkIsFree() || !gBlendCoop.partnerRoomOk || !gBlendLink.hostConnected) {
        gBlendCoop.regroupFailed = 1;
        return;
    }
    BLEND_DARK_WORLD = p->darkWorld;
    MemClear(&t, sizeof(t));
    t.warp_type = 0;
    t.endX = gBlendCoop.partnerRoomX;
    t.endY = gBlendCoop.partnerRoomY;
    t.area = p->area;
    t.room = p->room;
    t.layer = gBlendCoop.partnerLayer;
    t.facing_direction = gPlayerEntity.base.animationState >> 1;
    t.transitionSFX = SFX_TELEPORTER;
    DoExitTransition(&t);
}

void Blend_CoopUpdate(void) {
    Entity* link = &gPlayerEntity.base;
    InitBlock();
    gBlendCoop.heartbeat++;
    if (BLEND_REVIVE_FAIRIES > REVIVE_FAIRIES_MAX) {
        BLEND_REVIVE_FAIRIES = REVIVE_FAIRIES_MAX;
    }
    gBlendCoop.reviveFairies = BLEND_REVIVE_FAIRIES;
    if (gSave.stats.health != 0) {
        gBlendCoop.downed = 0;
        gBlendCoop.gaveUp = 0;
        gBlendCoop.giveUpTimer = 0;
    }
    if (!gBlendCoop.downed) {
        gBlendCoop.seenReviveSeq = gBlendCoop.partnerReviveSeq;
    }
    gBlendCoop.roomX = link->x.HALF.HI - gRoomControls.origin_x;
    gBlendCoop.roomY = link->y.HALF.HI - gRoomControls.origin_y;
    gBlendCoop.layer = link->collisionLayer;
    gBlendCoop.bombs = gSave.stats.bombCount;
    gBlendCoop.arrows = gSave.stats.arrowCount;
    gBlendCoop.messageOpen = (gMessage.state & MESSAGE_ACTIVE) != 0;
    {
        s32 slot = FindPalette(0);
        gBlendCoop.itemPalette = slot < 0 ? 0 : slot;
    }
    PublishMenu();
    HandleQuickEquip();
    HandleShareGive();
    HandleRegroup();
    UpdateRevivePartner();
    if (gBlendCoop.reviveFxTimer != 0) {
        gBlendCoop.reviveFxTimer--;
        if ((gBlendCoop.reviveFxTimer & 7) == 0) {
            SparkleAt(FX_SPARKLE2, link->x.HALF.HI + (Random() & 15) - 8, link->y.HALF.HI - (Random() & 15));
        }
    }
}
