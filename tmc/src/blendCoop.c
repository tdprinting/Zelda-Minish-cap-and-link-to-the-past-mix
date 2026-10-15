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
 * - Picking the partner up (R next to them) and throwing them (R again): the
 *   partner Link object is liftable like a pot, and the partner's game keeps
 *   its Link over our head, then sets it down where it landed.
 * - Giving the partner a heart (L + R next to them).
 * - Co-op puzzles in the overworld: Twin Stones that must all be lit at once
 *   (they stand too far apart for one player) and Toss Bells that ring only
 *   when a thrown partner flies into them.
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
#include "../custom/gen/coop/twin_relics.h"
#include "hitbox.h"
#include "tiles.h"

u32 FindFreeObjPalette(u32 paletteCount);
void SetEntityObjPalette(Entity* entity, s32 palette);
void CleanUpObjPalettes(void);
extern u32 gFrameObjLists[];
extern Frame* gSpriteAnimations_322[];
u32 Blend_SlotShareItem(u32 item);
void Blend_SetSlotShare(u32 item, bool32 on);
void RegisterCarryEntity(Entity*);

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

/* ---- Carrying and throwing the partner ---------------------------------------- */

/** How close the partner must stand to give them a heart. */
#define HEART_REACH 28
/** Frames the partner object stays where it landed while the partner's game catches up. */
#define CARRY_GRACE 6

/** This Link can be picked up: on foot, free, not down, not carrying anything. */
static void UpdateLiftable(void) {
    gBlendCoop.liftable = LinkIsFree() && !gBlendCoop.carried && !gBlendCoop.downed &&
                          !(gPlayerState.flags & (PL_MINISH | PL_IN_MINECART)) && gPlayerState.swim_state == 0 &&
                          gPlayerState.jump_status == 0 && gPlayerState.heldObject == 0;
}

/**
 * The partner Link object (type 0) as something to carry. While it stands
 * still it registers as liftable, like a pot; once lifted, the game's own
 * carrying and throwing move it, and this reports where it is to the
 * partner's game through the host. Returns TRUE while this owns its position.
 */
bool32 Blend_CoopPartnerCarry(Entity* this) {
    if (this->action == 2) {
        if (!PartnerHere() || !gBlendCoop.partnerInGame) {
            // the partner left: let go (the held object sees the action change and drops)
            this->action = 1;
            this->subAction = 0;
            COLLISION_OFF(this);
            gBlendCoop.carryState = CARRY_NONE;
            return FALSE;
        }
        switch (this->subAction) {
            case 0:
                // just lifted: like a thrown pot, a thrown partner hits what they fly into
                this->subAction = 1;
                COLLISION_ON(this);
                this->collisionFlags = 7;
                this->hitType = 1;
                this->collisionMask = gPlayerEntity.base.collisionMask;
                gBlendCoop.carryState = CARRY_HOLD;
                break;
            case 1:
                gBlendCoop.carryState = CARRY_HOLD;
                break;
            case 2:
                gBlendCoop.carryState = CARRY_FLY;
                break;
            default:
                // came down: the partner's game sets its Link down here
                gBlendCoop.carryState = CARRY_LANDED;
                gBlendCoop.carrySeq++;
                gBlendCoop.carryX = this->x.HALF.HI;
                gBlendCoop.carryY = this->y.HALF.HI;
                gBlendCoop.carryZ = 0;
                gBlendCoop.carryGrace = CARRY_GRACE;
                this->action = 1;
                this->subAction = 0;
                this->z.WORD = 0;
                COLLISION_OFF(this);
                return TRUE;
        }
        gBlendCoop.carryX = this->x.HALF.HI;
        gBlendCoop.carryY = this->y.HALF.HI;
        gBlendCoop.carryZ = this->z.HALF.HI;
        return TRUE;
    }
    if (gBlendCoop.carryGrace != 0) {
        gBlendCoop.carryGrace--;
        return TRUE;
    }
    gBlendCoop.carryState = CARRY_NONE;
    if (gBlendCoop.partnerLiftable && !Blend_CoopPartnerDowned() && !gBlendCoop.carried &&
        gPlayerState.heldObject == 0 && !(gBlendLink.heldKeys & L_BUTTON)) {
        // a light toss: the arc of a heavy pot (carryFlags 0x20), with one bounce
        this->hitbox = (Hitbox*)&gHitbox_18;
        this->carryFlags = 0x20;
        RegisterCarryEntity(this);
    }
    return FALSE;
}

/** Held up or thrown by the partner: this Link follows their game until it lands. */
static void UpdateCarried(void) {
    Entity* link = &gPlayerEntity.base;
    u32 state = gBlendCoop.partnerCarryState;
    bool32 here = PartnerHere() && gBlendCoop.partnerInGame;
    bool32 landed = gBlendCoop.partnerCarrySeq != gBlendCoop.seenCarrySeq;
    if (gBlendCoop.carried) {
        if (landed || !here || gSave.stats.health == 0 || (state != CARRY_HOLD && state != CARRY_FLY)) {
            if (landed && here) {
                link->x.HALF.HI = gBlendCoop.partnerCarryX;
                link->y.HALF.HI = gBlendCoop.partnerCarryY;
                SoundReq(SFX_PLY_LAND);
            }
            link->z.WORD = 0;
            link->zVelocity = 0;
            gPlayerState.field_0x14 = 0;
            gBlendCoop.carried = 0;
            gBlendCoop.seenCarrySeq = gBlendCoop.partnerCarrySeq;
            return;
        }
    } else {
        gBlendCoop.seenCarrySeq = gBlendCoop.partnerCarrySeq;
        if (!here || state != CARRY_HOLD || !gBlendCoop.liftable) {
            return;
        }
        gBlendCoop.carried = 1;
        SoundReq(SFX_PLY_JUMP);
    }
    // over the partner's head or in the air: keep still where their game holds us
    link->x.HALF.HI = gBlendCoop.partnerCarryX;
    link->y.HALF.HI = gBlendCoop.partnerCarryY;
    link->z.HALF.HI = gBlendCoop.partnerCarryZ;
    link->z.HALF.LO = 0;
    link->zVelocity = 0; // no gravity of our own: the partner's game flies us
    gPlayerState.field_0x14 = 1;
    gInput.heldKeys &= ~(DPAD_RIGHT | DPAD_LEFT | DPAD_UP | DPAD_DOWN | R_BUTTON | L_BUTTON);
    gInput.newKeys &= ~(DPAD_RIGHT | DPAD_LEFT | DPAD_UP | DPAD_DOWN | R_BUTTON | L_BUTTON);
}

/** L + R next to the partner gives them one of our hearts. */
static void UpdateGiveHeart(void) {
    BlendPlayerSync* p = &gBlendLink.partner;
    s32 dx, dy;
    if (!PartnerHere() || !gBlendCoop.partnerInGame || !LinkIsFree() || Blend_CoopPartnerDowned() ||
        gBlendCoop.carried || !(gBlendLink.heldKeys & L_BUTTON)) {
        return;
    }
    dx = (s32)p->x - gPlayerEntity.base.x.HALF.HI;
    dy = (s32)p->y - gPlayerEntity.base.y.HALF.HI;
    if (dx < -HEART_REACH || dx > HEART_REACH || dy < -HEART_REACH || dy > HEART_REACH) {
        return;
    }
    // with L held, R next to the partner gives instead of lifting
    gInput.heldKeys &= ~R_BUTTON;
    gInput.newKeys &= ~R_BUTTON;
    if (!(gBlendLink.newKeys & R_BUTTON)) {
        return;
    }
    if (gSave.stats.health <= 8 || gBlendCoop.partnerHealth + 4 > gBlendCoop.partnerMaxHealth) {
        SoundReq(SFX_MENU_ERROR);
        return;
    }
    ModHealth(-8);
    gBlendCoop.heartGiveSeq++;
    SoundReq(SFX_HEART_GET);
    SparkleAt(FX_SPARKLE2, p->x, p->y - 12);
}

/** A heart the partner gave us. */
static void UpdateReceiveHeart(void) {
    if (gBlendCoop.partnerHeartGiveSeq == gBlendCoop.seenHeartGiveSeq) {
        return;
    }
    gBlendCoop.seenHeartGiveSeq = gBlendCoop.partnerHeartGiveSeq;
    if (!PartnerHere() || gSave.stats.health == 0) {
        return;
    }
    ModHealth(8);
    SoundReq(SFX_HEART_GET);
    SparkleAt(FX_SPARKLE2, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI - 12);
    SparkleAt(FX_SPARKLE2, gPlayerEntity.base.x.HALF.HI - 6, gPlayerEntity.base.y.HALF.HI - 4);
}

/* ---- Co-op puzzles ------------------------------------------------------------ */

#define PIECE_STONE 0
#define PIECE_BELL 1
/** Frames a struck Twin Stone stays lit: too short to run between a puzzle's stones alone. */
#define STONE_LIT_FRAMES 60
/** A lit piece that stays lit (a rung bell, or every piece once solved). */
#define PIECE_LATCHED 0xff
/** How near a thrown partner must fly to a Toss Bell to ring it. */
#define BELL_REACH 14
/** Marks the puzzle pieces' palette in gPaletteList. */
#define TWIN_RELICS_PALETTE_ID 0x3f2

typedef struct {
    u8 area;
    u8 room;
    u8 count;      /**< pieces */
    u8 reward;     /**< the item each player finds once it is solved */
    u8 kind[4];    /**< PIECE_* */
    u16 x[4];      /**< room coordinates (moved to the nearest open ground) */
    u16 y[4];
    u16 rewardX;
    u16 rewardY;
} CoopPuzzle;

/** The overworld's co-op puzzles, one per room, in the Light World only. Bits of BLEND_COOP_PUZZLES_*. */
static const CoopPuzzle sCoopPuzzles[] = {
    // South Hyrule Field, around Link's house: a stone on each side of the yard
    { AREA_HYRULE_FIELD, 1, 2, ITEM_REVIVE_FAIRY,
      { PIECE_STONE, PIECE_STONE }, { 0x1a8, 0x360 }, { 0x1c8, 0x1b0 }, 0x290, 0x210 },
    // Hyrule Town: a bell in the south square, for a partner thrown from the road
    { AREA_HYRULE_TOWN, 0, 1, ITEM_RUPEE100,
      { PIECE_BELL }, { 0x200 }, { 0x330 }, 0x228, 0x330 },
    // Castle Garden: a stone deep in each hedge maze, and a bell before the castle
    { AREA_CASTLE_GARDEN, 0, 3, ITEM_REVIVE_FAIRY,
      { PIECE_STONE, PIECE_STONE, PIECE_BELL }, { 0xd0, 0x340, 0x200 }, { 0x110, 0x160, 0xa0 }, 0x200, 0xd0 },
};

static bool32 OpenGround(s32 x, s32 y) {
    u32 act;
    if (GetCollisionDataAtWorldCoords(x, y, 1) != 0) {
        return FALSE;
    }
    act = GetActTileAtWorldCoords(x, y, 1);
    return act != ACT_TILE_13 && act != ACT_TILE_16 && act != ACT_TILE_17 && act != ACT_TILE_19 && act != ACT_TILE_90;
}

/** The nearest open ground to a room spot, in world coordinates (the same in both games). */
static void FindOpenGround(u32 roomX, u32 roomY, s32* outX, s32* outY) {
    s32 x0 = gRoomControls.origin_x + roomX;
    s32 y0 = gRoomControls.origin_y + roomY;
    s32 r, dx, dy;
    for (r = 0; r <= 48; r += 8) {
        for (dy = -r; dy <= r; dy += 8) {
            for (dx = -r; dx <= r; dx += 8) {
                if ((dx == -r || dx == r || dy == -r || dy == r) && OpenGround(x0 + dx, y0 + dy)) {
                    *outX = x0 + dx;
                    *outY = y0 + dy;
                    return;
                }
            }
        }
    }
    *outX = x0;
    *outY = y0;
}

static const CoopPuzzle* PuzzleHere(u32* index) {
    u32 i;
    if (BLEND_DARK_WORLD) {
        return NULL;
    }
    for (i = 0; i < ARRAY_COUNT(sCoopPuzzles); i++) {
        if (sCoopPuzzles[i].area == gRoomControls.area && sCoopPuzzles[i].room == gRoomControls.room) {
            *index = i;
            return &sCoopPuzzles[i];
        }
    }
    return NULL;
}

static s32 LoadTwinRelicsPalette(void) {
    s32 slot = FindPalette(TWIN_RELICS_PALETTE_ID);
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
        gPaletteList[slot].objPaletteId = TWIN_RELICS_PALETTE_ID;
        gPaletteList[slot]._0_4 = 1;
        gPaletteList[slot]._0_0 = 3;
        gPaletteList[slot]._1 = 0;
    }
    dst = &gPaletteBuffer[(16 + slot) * 16];
    for (i = 0; i < 16; i++) {
        dst[i] = sTwinRelicsPalette[i];
    }
    USE_PALETTE(16 + slot);
    return slot;
}

static bool32 PieceLit(u32 i) {
    return gBlendCoop.pieceTimer[i] != 0 || (PartnerHere() && (gBlendCoop.partnerPiecesLit & (1 << i)));
}

/** A thrown partner (in either game) is flying within reach of this spot. */
static bool32 ThrownPartnerNear(s32 x, s32 y) {
    s32 fx, fy;
    if (gBlendCoop.carryState == CARRY_FLY) {
        fx = gBlendCoop.carryX;
        fy = gBlendCoop.carryY;
    } else if (gBlendCoop.carried && gBlendCoop.partnerCarryState == CARRY_FLY) {
        fx = gPlayerEntity.base.x.HALF.HI;
        fy = gPlayerEntity.base.y.HALF.HI;
    } else {
        return FALSE;
    }
    fx -= x;
    fy -= y;
    return fx > -BELL_REACH && fx < BELL_REACH && fy > -BELL_REACH && fy < BELL_REACH;
}

/**
 * Types 5 and 6 of the partner Link object: a Twin Stone and a Toss Bell
 * (type2 is the piece's number in its puzzle).
 */
void Blend_CoopPuzzlePiece(Entity* this) {
    u32 i = this->type2 & 3;
    u32 frame;
    bool32 lit;
    if (this->action == 0) {
        s32 slot = LoadTwinRelicsPalette();
        this->action = 1;
        this->spriteSettings.draw = 1;
        this->spriteSettings.shadow = 1;
        this->lastFrameIndex = 0xff;
        if (slot >= 0) {
            SetEntityObjPalette(this, slot);
        }
        if (this->type == 5) {
            // struck like a switch, by Link's sword (or the partner's)
            COLLISION_ON(this);
            this->collisionFlags = 7;
            this->hurtType = 0x48;
            this->hitType = 0x28;
            this->collisionMask = 10;
            this->hitbox = (Hitbox*)&gHitbox_0;
        } else {
            COLLISION_OFF(this);
        }
        UpdateSpriteForCollisionLayer(this);
    }
    if (this->type == 5) {
        if ((this->contactFlags & CONTACT_NOW) && gBlendCoop.pieceTimer[i] != PIECE_LATCHED) {
            if (gBlendCoop.pieceTimer[i] == 0) {
                SoundReq(SFX_110);
            }
            gBlendCoop.pieceTimer[i] = STONE_LIT_FRAMES;
        }
        this->contactFlags = 0;
        lit = PieceLit(i);
        frame = lit ? 1 + ((gRoomTransition.frameCount >> 3) & 1) : 0;
    } else {
        if (gBlendCoop.pieceTimer[i] != PIECE_LATCHED && ThrownPartnerNear(this->x.HALF.HI, this->y.HALF.HI)) {
            gBlendCoop.pieceTimer[i] = PIECE_LATCHED;
            this->subtimer = 90;
            SoundReq(SFX_ITEM_SWORD_CHARGE_FINISH);
            SparkleAt(FX_SPARKLE2, this->x.HALF.HI, this->y.HALF.HI - 16);
        }
        lit = PieceLit(i);
        if (lit && this->subtimer == 0 && !(gBlendCoop.pieceTimer[i] == PIECE_LATCHED)) {
            // the partner's game rang it: ring here too
            this->subtimer = 90;
            gBlendCoop.pieceTimer[i] = PIECE_LATCHED;
            SoundReq(SFX_ITEM_SWORD_CHARGE_FINISH);
        }
        if (this->subtimer != 0) {
            static const u8 sRing[] = { 4, 3, 5, 3 };
            this->subtimer--;
            frame = sRing[(this->subtimer >> 2) & 3];
        } else {
            frame = 3;
        }
    }
    this->spriteIndex = TWIN_RELICS_SPRITE;
    this->frameIndex = frame;
    if (this->frameIndex != this->lastFrameIndex) {
        this->lastFrameIndex = this->frameIndex;
        sub_080042D0(this, this->frameIndex, TWIN_RELICS_SPRITE);
    }
}

static void SpawnPuzzleReward(const CoopPuzzle* pz) {
    s32 x, y;
    Entity* e = CreateObject(GROUND_ITEM, pz->reward, 0);
    if (e != NULL) {
        FindOpenGround(pz->rewardX, pz->rewardY, &x, &y);
        e->timer = 4; // lies in the open (TREASURE_OPEN)
        e->x.HALF.HI = x;
        e->y.HALF.HI = y;
        e->collisionLayer = 1;
    }
    gBlendCoop.reward = e;
}

/** Sets out this room's puzzle when Link arrives, and watches for it being solved. */
void Blend_CoopPuzzlesUpdate(void) {
    const CoopPuzzle* pz;
    u32 index = 0, i, key, lit;
    key = ((gRoomControls.area << 8) | gRoomControls.room) + 1;
    if (gBlendCoop.puzzleRoomKey != key) {
        // a new room: wait until Link is in it and free before setting anything out
        if (gPlayerState.controlMode != CONTROL_ENABLED || !gBlendLink.hostConnected) {
            return;
        }
        gBlendCoop.puzzleRoomKey = key;
        gBlendCoop.piecesLit = 0;
        gBlendCoop.reward = NULL;
        MemClear(gBlendCoop.pieceTimer, sizeof(gBlendCoop.pieceTimer));
        pz = PuzzleHere(&index);
        if (pz == NULL) {
            return;
        }
        gBlendCoop.puzzleSolved = (BLEND_COOP_PUZZLES_SOLVED >> index) & 1;
        for (i = 0; i < pz->count; i++) {
            Entity* e = CreateObject(LINK_PARTNER, pz->kind[i] == PIECE_BELL ? 6 : 5, i);
            if (e != NULL) {
                s32 x, y;
                FindOpenGround(pz->x[i], pz->y[i], &x, &y);
                e->x.HALF.HI = x;
                e->y.HALF.HI = y;
                e->collisionLayer = 1;
            }
            if (gBlendCoop.puzzleSolved) {
                gBlendCoop.pieceTimer[i] = PIECE_LATCHED;
            }
        }
        if (gBlendCoop.puzzleSolved && !((BLEND_COOP_PUZZLES_TAKEN >> index) & 1)) {
            SpawnPuzzleReward(pz);
        }
        return;
    }
    pz = PuzzleHere(&index);
    if (pz == NULL) {
        gBlendCoop.piecesLit = 0;
        return;
    }
    lit = 0;
    for (i = 0; i < pz->count; i++) {
        if (gBlendCoop.pieceTimer[i] != 0 && gBlendCoop.pieceTimer[i] != PIECE_LATCHED) {
            gBlendCoop.pieceTimer[i]--;
        }
        if (gBlendCoop.pieceTimer[i] != 0) {
            lit |= 1 << i;
        }
    }
    gBlendCoop.piecesLit = lit;
    if (!gBlendCoop.puzzleSolved) {
        for (i = 0; i < pz->count; i++) {
            if (!PieceLit(i)) {
                break;
            }
        }
        if (i == pz->count) {
            // solved together: everything stays lit, and the reward appears for each player
            gBlendCoop.puzzleSolved = 1;
            BLEND_COOP_PUZZLES_SOLVED |= 1 << index;
            for (i = 0; i < pz->count; i++) {
                gBlendCoop.pieceTimer[i] = PIECE_LATCHED;
            }
            SoundReq(SFX_SECRET);
            SpawnPuzzleReward(pz);
        }
    } else if (gBlendCoop.reward != NULL) {
        Entity* e = gBlendCoop.reward;
        if (e->next == NULL || e->kind != OBJECT || e->id != GROUND_ITEM) {
            // picked up
            gBlendCoop.reward = NULL;
            BLEND_COOP_PUZZLES_TAKEN |= 1 << index;
        }
    }
}

void Blend_CoopUpdate(void) {
    Entity* link = &gPlayerEntity.base;
    InitBlock();
    gBlendCoop.menuPage = 0;
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
    UpdateCarried();
    UpdateGiveHeart();
    UpdateReceiveHeart();
    UpdateRevivePartner();
    UpdateLiftable();
    Blend_CoopPuzzlesUpdate();
    if (gBlendCoop.reviveFxTimer != 0) {
        gBlendCoop.reviveFxTimer--;
        if ((gBlendCoop.reviveFxTimer & 7) == 0) {
            SparkleAt(FX_SPARKLE2, link->x.HALF.HI + (Random() & 15) - 8, link->y.HALF.HI - (Random() & 15));
        }
    }
}
