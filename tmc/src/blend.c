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
 * - A Link to the Past items: Magic Mirror, Moon Pearl (Link is a bunny in the
 *   Dark World without it), Fire Rod, and the Bombos, Ether and Quake medallions.
 * - Dev hooks used by the automated test harness.
 */
#include "blend.h"
#include "area.h"
#include "common.h"
#include "entity.h"
#include "fade.h"
#include "game.h"
#include "main.h"
#include "flags.h"
#include "message.h"
#include "player.h"
#include "room.h"
#include "save.h"
#include "sound.h"
#include "enemy.h"
#include "collision.h"
#include "physics.h"
#include "color.h"
#include "object.h"
#include "item.h"
#include "effects.h"
#include "playeritem.h"
#include "asm.h"
#include "ui.h"
#include "itemDefinitions.h"

#define MIRROR_HOLD_FRAMES 8

u32 FindFreeObjPalette(u32 paletteCount);
void SetEntityObjPalette(Entity* entity, s32 palette);

extern u32 gUsedPalettes;

bool32 Blend_IsDarkWorldActive(void) {
    return BLEND_DARK_WORLD && (gArea.areaMetadata & AR_IS_OVERWORLD);
}

static bool32 HasItem(u32 item) {
    return GetInventoryValue(item) != 0;
}

/** Like A Link to the Past, Link turns into a bunny in the Dark World until he finds the Moon Pearl. */
static bool32 IsBunny(void) {
    return Blend_IsDarkWorldActive() && !HasItem(ITEM_MOON_PEARL);
}

/** The bunny's pink coat: Link's green tunic shades turn pink. */
static void BunnyTint(u16* pal) {
    u32 i;
    for (i = 0; i < 16; i++) {
        u32 c = pal[i];
        u32 r = c & 0x1F;
        u32 g = (c >> 5) & 0x1F;
        u32 b = (c >> 10) & 0x1F;
        if (g > r + 2 && g > b + 2) {
            r = g;
            b = (g * 3) >> 2;
            g = g >> 1;
            pal[i] = r | (g << 5) | (b << 10);
        }
    }
}

/** Link's and the partner's OBJ palettes keep their colors in the Dark World. */
static bool32 IsPaletteExempt(u32 palIdx) {
    u32 id;
    if (palIdx == 16u + gPlayerEntity.base.palette.b.b0 ||
        (gBlendLink.partnerPalette != 0 && palIdx == 16u + gBlendLink.partnerPalette)) {
        return TRUE;
    }
    // A Link to the Past's enemies already wear their own Dark World colors
    if (palIdx >= 16) {
        id = gPaletteList[palIdx - 16].objPaletteId;
        // and the Golden Sword's gold (0x3f2-0x3f4) stays gold
        return (id >= 0x3d0 && id < 0x3f0) || (id >= 0x3f2 && id <= 0x3f4);
    }
    return FALSE;
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
        if (gBlendLink.bunny && palIdx == 16u + gPlayerEntity.base.palette.b.b0) {
            BunnyTint(pal);
        }
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

/** Link is standing or walking around, free to act. */
static bool32 IsPlayerFree(void) {
    if (gMessage.state & MESSAGE_ACTIVE)
        return FALSE;
    if (gPlayerState.framestate != PL_STATE_IDLE && gPlayerState.framestate != PL_STATE_WALK)
        return FALSE;
    return gPlayerState.controlMode == CONTROL_ENABLED;
}

static bool32 CanUseMirror(void) {
    if (!(gArea.areaMetadata & AR_IS_OVERWORLD))
        return FALSE;
    return HasItem(ITEM_MAGIC_MIRROR) && IsPlayerFree();
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

/** Dark World versions of overworld enemies: A Link to the Past's Rope and Moblin, and darker Minish Cap ones. */
static u32 DarkWorldEnemy(u32 id) {
    switch (id) {
        case OCTOROK:
            return ALTTP_ROPE;
        case SPEAR_MOBLIN:
            return ALTTP_MOBLIN;
        case CHUCHU:
            return SPINY_CHUCHU;
        case BEETLE:
            return SPIKED_BEETLE;
        case PEAHAT:
            // the Dark World's Eastern Hills and Death Mountain (Mt. Crenel) have Hinoxes
            if (gRoomControls.area == AREA_HYRULE_FIELD || gRoomControls.area == AREA_MT_CRENEL) {
                return ALTTP_HINOX;
            }
            return id;
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

static bool32 StartItemAction(u32 item);

/** Called when Link holds the Mirror up high: he goes through to the other world. */
static void UseMirror(void) {
    BLEND_DARK_WORLD ^= 1;
    SoundReq(SFX_TELEPORTER);
    WarpInPlace();
}

/** Holding L + R raises the Magic Mirror, as in A Link to the Past; the warp follows. */
static void UpdateMirror(void) {
    u32 held = gBlendLink.heldKeys;
    if ((held & L_BUTTON) && HasItem(ITEM_MAGIC_MIRROR) && (gArea.areaMetadata & AR_IS_OVERWORLD)) {
        // with L held, R belongs to the Mirror: Link must not grab or roll first
        gInput.heldKeys &= ~R_BUTTON;
        gInput.newKeys &= ~R_BUTTON;
    }
    if ((held & (L_BUTTON | R_BUTTON)) == (L_BUTTON | R_BUTTON) && CanUseMirror()) {
        if (++gBlendLink.mirrorTimer == MIRROR_HOLD_FRAMES) {
            StartItemAction(ITEM_MAGIC_MIRROR);
        }
    } else {
        gBlendLink.mirrorTimer = 0;
    }
}


/**
 * The picture shown for an item on the ground or held up by Link. The new
 * items borrow Minish Cap pictures: the Mirror Shield for the Magic Mirror,
 * and the glowing Elements for the Moon Pearl and the medallions.
 */
/** Pictures for the shared-slot items, in item id order from ITEM_HOOKSHOT. */
static const u8 sSlotShareIcons[] = { ITEM_LIGHT_ARROW, ITEM_SMITH_SWORD, ITEM_PACCI_CANE, ITEM_BOTTLE_FAIRY, ITEM_FIRE_ROD };

u32 Blend_ItemSpriteAnim(u32 item) {
    switch (item) {
        case ITEM_MAGIC_MIRROR:
            return ITEM_MIRROR_SHIELD;
        case ITEM_MOON_PEARL:
            return ITEM_WATER_ELEMENT;
        case ITEM_BOMBOS:
            return ITEM_FIRE_ELEMENT;
        case ITEM_ETHER:
            return ITEM_WIND_ELEMENT;
        case ITEM_QUAKE:
            return ITEM_EARTH_ELEMENT;
        case ITEM_ICE_ROD:
            return ITEM_FIRE_ROD;
        case ITEM_GOLDEN_SWORD:
            return ITEM_FOURSWORD; // gilded by blendGoldenSword.c
        default:
            if (item >= ITEM_HOOKSHOT && item <= ITEM_CANE_OF_SOMARIA) {
                return sSlotShareIcons[item - ITEM_HOOKSHOT];
            }
            return item;
    }
}

/* ---- Fire Rod ---------------------------------------------------------- */

ItemBehavior* CreateItem(Item itemId);
extern ItemDefinition gItemDefinitions[];

/**
 * Plays one of Link's A Link to the Past item animations for this item. The item
 * is drawn into Link's own frames, so the Pacci Cane he would hold stays away.
 */
static void StartItemAnim(ItemBehavior* this, u32 index, u32 anim) {
    this->priority |= 0xf;
    sub_08077D38(this, index);
    SetItemAnim(this, anim);
    sub_0806F948(&gPlayerEntity.base);
    if (index == 0) {
        gPlayerState.item = NULL;
    }
    this->subtimer = 0;
}

/**
 * The Fire Rod and Ice Rod: Link raises the rod and swings it forward, as in A Link
 * to the Past, and it shoots the bolt Minish Cap already had for the Mirror Shield,
 * colored red or ice white.
 */
void ItemFireRod(ItemBehavior* this, u32 index) {
    if (this->stateID == 0) {
        StartItemAnim(this, index, this->behaviorId == ITEM_FIRE_ROD ? ANIM_BLEND_FIRE_ROD : ANIM_BLEND_ICE_ROD);
        return;
    }
    if (this->playerFrame & 0x80) {
        DeleteItemBehavior(this, index);
        return;
    }
    // one fireball per swing, and one on screen at a time
    if ((this->playerFrame & 0x40) && !this->subtimer &&
        FindEntity(PLAYER_ITEM, PLAYER_ITEM_FIRE_ROD_PROJECTILE, 2, 0, 0) == NULL) {
        Entity* fire = CreatePlayerItem(PLAYER_ITEM_FIRE_ROD_PROJECTILE, 0, 0, this->behaviorId);
        if (fire != NULL) {
            fire->parent = &gPlayerEntity.base;
            fire->animationState = gPlayerEntity.base.animationState;
            fire->collisionLayer = gPlayerEntity.base.collisionLayer;
            this->subtimer = 1;
        }
    }
    UpdateItemAnim(this);
}

static const u8 sRods[] = { ITEM_PACCI_CANE, ITEM_FIRE_ROD, ITEM_ICE_ROD };

static bool32 SwitchSharedSlot(u32 slot);

/**
 * L + A or L + B switches the item on that button: a shared slot goes to its
 * other item, and a rod goes to the next rod Link owns (Pacci Cane, Fire Rod, Ice Rod).
 */
static void UpdateRodSwap(void) {
    u32 slot;
    u32 item;
    u32 i, j;
    u32 newKeys = gBlendLink.newKeys;
    if (!(gBlendLink.heldKeys & L_BUTTON)) {
        return;
    }
    // with L held, A and B belong to switching; the game never sees them
    gInput.heldKeys &= ~(A_BUTTON | B_BUTTON);
    gInput.newKeys &= ~(A_BUTTON | B_BUTTON);
    if (!IsPlayerFree()) {
        return;
    }
    if (newKeys & A_BUTTON) {
        slot = SLOT_A;
    } else if (newKeys & B_BUTTON) {
        slot = SLOT_B;
    } else {
        return;
    }
    item = gSave.stats.equipped[slot];
    if (SwitchSharedSlot(slot)) {
        SoundReq(SFX_TEXTBOX_SELECT);
        return;
    }
    for (i = 0; i < ARRAY_COUNT(sRods); i++) {
        if (sRods[i] == item) {
            break;
        }
    }
    if (i == ARRAY_COUNT(sRods)) {
        return;
    }
    item = ITEM_NONE;
    for (j = 1; j < ARRAY_COUNT(sRods); j++) {
        u32 next = i + j;
        if (next >= ARRAY_COUNT(sRods)) {
            next -= ARRAY_COUNT(sRods);
        }
        if (HasItem(sRods[next])) {
            item = sRods[next];
            break;
        }
    }
    if (item == ITEM_NONE) {
        return;
    }
    ForceEquipItem(item, slot);
    SoundReq(SFX_TEXTBOX_SELECT);
}

/* ---- Items that share a slot with a Minish Cap item ---------------------- */

/**
 * The rest of A Link to the Past's items each share a menu slot with the Minish
 * Cap item closest in spirit. L + that item's button switches between the two.
 */
typedef struct {
    u8 item;
    u8 partner;
    u8 partnerAlt; /**< the partner's other form (Mirror Shield, lit lantern...) */
} SlotShare;

static const SlotShare sSlotShares[] = {
    { ITEM_HOOKSHOT, ITEM_GUST_JAR, ITEM_GUST_JAR },
    { ITEM_HAMMER, ITEM_MOLE_MITTS, ITEM_MOLE_MITTS },
    { ITEM_CANE_OF_BYRNA, ITEM_SHIELD, ITEM_MIRROR_SHIELD },
    { ITEM_MAGIC_POWDER, ITEM_LANTERN_OFF, ITEM_LANTERN_ON },
    { ITEM_CANE_OF_SOMARIA, ITEM_BOMBS, ITEM_REMOTE_BOMBS },
};

static const SlotShare* FindSlotShare(u32 partner) {
    u32 i;
    for (i = 0; i < ARRAY_COUNT(sSlotShares); i++) {
        if (sSlotShares[i].partner == partner || sSlotShares[i].partnerAlt == partner) {
            return &sSlotShares[i];
        }
    }
    return NULL;
}

static u32 VariantBit(u32 item) {
    return 1 << (item - ITEM_HOOKSHOT);
}

u32 Blend_ItemVariant(u32 item) {
    const SlotShare* s = FindSlotShare(item);
    if (s != NULL && HasItem(s->item) && (BLEND_VARIANTS_ON & VariantBit(s->item))) {
        return s->item;
    }
    return ITEM_NONE;
}

u32 Blend_HudIcon(u32 item) {
    u32 variant;
    if (gBlendLink.devHudIcon) {
        return gBlendLink.devHudIcon;
    }
    variant = Blend_ItemVariant(item);
    if (variant != ITEM_NONE) {
        return sSlotShareIcons[variant - ITEM_HOOKSHOT];
    }
    return item;
}

/** L + A or L + B on a shared slot switches between the two items. */
static bool32 SwitchSharedSlot(u32 slot) {
    u32 item = gSave.stats.equipped[slot];
    const SlotShare* s = FindSlotShare(item);
    if (s == NULL || !HasItem(s->item)) {
        return FALSE;
    }
    BLEND_VARIANTS_ON ^= VariantBit(s->item);
    gHUD.unk_13 = 0x7f;
    gHUD.unk_14 = 0x7f;
    return TRUE;
}

static const s8 sFacingX[] = { 0, 1, 0, -1 };
static const s8 sFacingY[] = { -1, 0, 1, 0 };

/** A point dist pixels in front of Link. */
static void PointInFront(s32 dist, s32* x, s32* y) {
    u32 facing = (gPlayerEntity.base.animationState >> 1) & 3;
    *x = gPlayerEntity.base.x.HALF.HI + sFacingX[facing] * dist;
    *y = gPlayerEntity.base.y.HALF.HI + sFacingY[facing] * dist;
}

/** Puts an invisible strike at a point (see LinkSpellHit). */
static Entity* StrikeAt(s32 x, s32 y, u32 damage) {
    Entity* hit = CreateObject(LINK_PARTNER, 2, 0);
    if (hit != NULL) {
        hit->x.HALF.HI = x;
        hit->y.HALF.HI = y;
        hit->collisionLayer = gPlayerEntity.base.collisionLayer;
        hit->type2 = damage;
    }
    return hit;
}

static Entity* FxAt(u32 fx, s32 x, s32 y) {
    Entity* e = CreateFx(&gPlayerEntity.base, fx, 0);
    if (e != NULL) {
        e->x.HALF.HI = x;
        e->y.HALF.HI = y;
    }
    return e;
}

static Entity* NearestEnemy(s32 x, s32 y, s32 range) {
    u32 i;
    Entity* e;
    Entity* best = NULL;
    for (i = 0; i < ARRAY_COUNT(gEntityLists); i++) {
        LinkedList* list = &gEntityLists[i];
        for (e = list->first; (u32)e != (u32)list; e = e->next) {
            s32 dx, dy;
            if (e->kind != ENEMY || !(e->flags & ENT_COLLIDE) || e->health == 0) {
                continue;
            }
            dx = e->x.HALF.HI - x;
            dy = e->y.HALF.HI - y;
            if (dx < 0)
                dx = -dx;
            if (dy < 0)
                dy = -dy;
            if (dx <= range && dy <= range) {
                best = e;
                range = dx > dy ? dx : dy;
            }
        }
    }
    return best;
}

enum { HOOK_NONE, HOOK_OUT, HOOK_PULL, HOOK_BACK };
#define HOOK_SPEED 4
#define HOOK_RANGE 128

static void UseHookshot(void) {
    Entity* hook;
    if (gBlendLink.hookState != HOOK_NONE) {
        return;
    }
    hook = CreateObject(LINK_PARTNER, 3, 0);
    if (hook != NULL) {
        CopyPosition(&gPlayerEntity.base, hook);
        hook->animationState = gPlayerEntity.base.animationState & 6;
        gBlendLink.hook = hook;
        gBlendLink.hookState = HOOK_OUT;
        SoundReq(SFX_ITEM_SWORD_BEAM);
    }
}

static void EndHook(Entity* this) {
    gBlendLink.hookState = HOOK_NONE;
    gBlendLink.hook = NULL;
    gPlayerEntity.base.z.HALF.HI = 0;
    DeleteThisEntity();
}

static const Hitbox sHookHitbox = { 0, 0, { 0, 0, 0, 0 }, 6, 6 };

/**
 * Type 3: the Hookshot's head. It flies out; if it bites into a wall with open
 * ground in front, it pulls Link across whatever lies between (pits, water).
 * It stuns and hurts an enemy it hits, and comes back otherwise.
 */
static void LinkHook(Entity* this) {
    Entity* p = &gPlayerEntity.base;
    u32 facing = this->animationState >> 1;
    s32 dx = sFacingX[facing];
    s32 dy = sFacingY[facing];
    if (this->action == 0) {
        this->action = 1;
        this->spriteSettings.draw = 0;
        this->hitbox = (Hitbox*)&sHookHitbox;
        this->flags |= ENT_COLLIDE;
        this->hurtType = 0x09;
        this->damage = 4;
        this->collisionFlags = (p->collisionFlags + 1) | 0x20;
        this->collisionMask = p->collisionMask;
        this->timer = 0;
        sub_0801766C(this);
    }
    switch (gBlendLink.hookState) {
        case HOOK_OUT:
            this->x.HALF.HI += dx * HOOK_SPEED;
            this->y.HALF.HI += dy * HOOK_SPEED;
            this->timer += HOOK_SPEED;
            if ((this->timer & 15) == 0) {
                CreateFx(this, FX_SPARKLE2, 0); // the chain
            }
            if (this->contactFlags & CONTACT_NOW) {
                gBlendLink.hookState = HOOK_BACK;
            } else if (GetCollisionDataAtWorldCoords(this->x.HALF.HI, this->y.HALF.HI, p->collisionLayer) != 0) {
                s32 stopX = this->x.HALF.HI - dx * 12;
                s32 stopY = this->y.HALF.HI - dy * 12;
                if (this->timer > 24 && GetCollisionDataAtWorldCoords(stopX, stopY, p->collisionLayer) == 0) {
                    gBlendLink.hookState = HOOK_PULL;
                    this->flags &= ~ENT_COLLIDE;
                    CreateFx(this, FX_REFLECT, 0);
                    SoundReq(SFX_HAMMER2);
                } else {
                    gBlendLink.hookState = HOOK_BACK;
                    CreateFx(this, FX_REFLECT, 0);
                }
            } else if (this->timer >= HOOK_RANGE) {
                gBlendLink.hookState = HOOK_BACK;
            }
            break;
        case HOOK_PULL: {
            s32 left = (this->x.HALF.HI - p->x.HALF.HI) * dx + (this->y.HALF.HI - p->y.HALF.HI) * dy;
            if (left <= 12) {
                EndHook(this);
            }
            p->x.HALF.HI += dx * HOOK_SPEED;
            p->y.HALF.HI += dy * HOOK_SPEED;
            p->z.HALF.HI = -4; // held off the ground, over pits and water
            break;
        }
        default: {
            s32 left = (this->x.HALF.HI - p->x.HALF.HI) * dx + (this->y.HALF.HI - p->y.HALF.HI) * dy;
            this->flags &= ~ENT_COLLIDE;
            if (left <= 8 || gBlendLink.hookState != HOOK_BACK) {
                EndHook(this);
            }
            this->x.HALF.HI -= dx * (HOOK_SPEED + 2);
            this->y.HALF.HI -= dy * (HOOK_SPEED + 2);
            break;
        }
    }
}

static void UseHammer(void) {
    s32 x, y;
    PointInFront(14, &x, &y);
    StrikeAt(x, y, 32);
    FxAt(FX_ROCK, x, y);
    DoTileInteraction(&gPlayerEntity.base, 3, x, y); // smashes rocks and pots like a blast
    InitScreenShake(10, 1);
    SoundReq(SFX_HAMMER1);
}

static void UseByrna(void) {
    if (gBlendLink.byrnaTimer) {
        gBlendLink.byrnaTimer = 0;
        return;
    }
    gBlendLink.byrnaTimer = 600;
    SoundReq(SFX_SUMMON);
}

/** The Cane of Byrna's ring: Link can't be hurt, and the ring stings enemies it touches. */
/* ---- Ice Rod: frozen enemies -------------------------------------------- */

#define FREEZE_FRAMES 180

/**
 * The Ice Rod's bolt freezes the enemy it hits, as in A Link to the Past. A frozen
 * enemy turns icy white and stops (Minish Cap skips the update of an enemy that is
 * marked as held), but it can still be struck. Bosses shrug it off.
 */
/**
 * Copies an OBJ palette into a free slot as ice: every shade becomes a pale,
 * frosty blue of the same brightness. Returns the slot, or 0 if none is free.
 */
static u32 LoadIcePalette(u32 from) {
    u32 slot = FindFreeObjPalette(1);
    u16* src = &gPaletteBuffer[(16 + from) * 16];
    u16* dst;
    u32 i;
    if (slot >= 16) {
        return 0;
    }
    gPaletteList[slot].objPaletteId = 0x3f1;
    gPaletteList[slot]._0_4 = 1;
    gPaletteList[slot]._0_0 = 3;
    gPaletteList[slot]._1 = 1;
    dst = &gPaletteBuffer[(16 + slot) * 16];
    for (i = 0; i < 16; i++) {
        u32 c = src[i];
        // brightness, weighted 3:6:1; times 205 / 2048 divides by ten without a division routine
        u32 lum = (((c & 0x1F) * 3 + ((c >> 5) & 0x1F) * 6 + ((c >> 10) & 0x1F)) * 205) >> 11;
        u32 r = (lum >> 1) + 10;
        u32 g = ((lum * 3) >> 2) + 8;
        u32 b = lum + 12;
        if (b > 31) {
            b = 31;
        }
        dst[i] = r | (g << 5) | (b << 10);
    }
    USE_PALETTE(16 + slot);
    return slot;
}

void Blend_FreezeEnemy(Entity* enemy) {
    u32 i, slot = 4;
    if (enemy == NULL || enemy->kind != ENEMY || enemy->health == 0 ||
        (((Enemy*)enemy)->enemyFlags & (EM_FLAG_BOSS | EM_FLAG_SUPPORT))) {
        return;
    }
    for (i = 0; i < 4; i++) {
        if (gBlendLink.frozen[i] == enemy) {
            return;
        }
        if (gBlendLink.frozen[i] == NULL && slot == 4) {
            slot = i;
        }
    }
    if (slot == 4) {
        return;
    }
    gBlendLink.frozen[slot] = enemy;
    gBlendLink.freezeTimer[slot] = FREEZE_FRAMES;
    gBlendLink.freezePalette[slot] = enemy->palette.b.b0;
    gBlendLink.icePalette[slot] = LoadIcePalette(enemy->palette.b.b0);
    ((Enemy*)enemy)->enemyFlags |= EM_FLAG_SUPPORT;
    SoundReq(SFX_ICE_BLOCK_SLIDE);
}

static void SetFrozenLook(Entity* e, u32 i, bool32 icy) {
    u32 pal = icy && gBlendLink.icePalette[i] != 0 ? gBlendLink.icePalette[i] : gBlendLink.freezePalette[i];
    e->palette.b.b0 = pal;
}

static void Thaw(u32 i) {
    Entity* e = gBlendLink.frozen[i];
    if (e->kind == ENEMY && e->next != NULL) {
        ((Enemy*)e)->enemyFlags &= ~EM_FLAG_SUPPORT;
        SetFrozenLook(e, i, FALSE);
    }
    if (gBlendLink.icePalette[i] != 0) {
        sub_0801D244(gBlendLink.icePalette[i]);
    }
    gBlendLink.frozen[i] = NULL;
    gBlendLink.freezeTimer[i] = 0;
    gBlendLink.icePalette[i] = 0;
}

static void UpdateFrozen(void) {
    u32 i;
    for (i = 0; i < 4; i++) {
        Entity* e = gBlendLink.frozen[i];
        if (e == NULL) {
            continue;
        }
        if (e->kind != ENEMY || e->next == NULL || e->health == 0 || --gBlendLink.freezeTimer[i] == 0) {
            // gone, struck down (it shatters as it thaws), or melted
            Thaw(i);
        } else {
            // flicker before thawing
            SetFrozenLook(e, i, gBlendLink.freezeTimer[i] >= 40 || !(gBlendLink.freezeTimer[i] & 4));
        }
    }
}

static void ForgetFrozen(void) {
    u32 i;
    for (i = 0; i < 4; i++) {
        // the room's palettes were reset with the room, so nothing is left to release
        gBlendLink.frozen[i] = NULL;
        gBlendLink.freezeTimer[i] = 0;
        gBlendLink.icePalette[i] = 0;
    }
}

static void UpdateByrna(void) {
    static const s8 sRingX[] = { 0, 14, 20, 14, 0, -14, -20, -14 };
    static const s8 sRingY[] = { -20, -14, 0, 14, 20, 14, 0, -14 };
    Entity* p = &gPlayerEntity.base;
    u32 step;
    if (gBlendLink.byrnaTimer == 0) {
        return;
    }
    if (--gBlendLink.byrnaTimer == 0 || gBlendLink.bunny) {
        gBlendLink.byrnaTimer = 0;
        return;
    }
    if (p->iframes < 2) {
        p->iframes = 2;
    }
    if ((gBlendLink.byrnaTimer & 3) == 0) {
        step = (gBlendLink.byrnaTimer >> 2) & 7;
        StrikeAt(p->x.HALF.HI + sRingX[step], p->y.HALF.HI + sRingY[step], 8);
        FxAt(FX_SPARKLE2, p->x.HALF.HI + sRingX[step], p->y.HALF.HI + sRingY[step]);
    }
}

/** Magic Powder turns a weak enemy into a fairy, and stings stronger ones. */
static void UsePowder(void) {
    s32 x, y;
    Entity* e;
    PointInFront(16, &x, &y);
    FxAt(FX_SPARKLE, x, y);
    FxAt(FX_SPARKLE2, x - 6, y + 4);
    FxAt(FX_SPARKLE2, x + 6, y - 4);
    SoundReq(SFX_SPARKLES);
    e = NearestEnemy(x, y, 20);
    if (e == NULL) {
        return;
    }
    if (e->health <= 8) {
        Entity* fairy = CreateObject(FAIRY, 0x60, 0);
        if (fairy != NULL) {
            fairy->timer = 0;
            CopyPosition(e, fairy);
        }
        CreateFx(e, FX_DEATH, 0);
        DeleteEntity(e);
    } else {
        StrikeAt(e->x.HALF.HI, e->y.HALF.HI, 8);
    }
}

/** The Cane of Somaria conjures one pushable block per room, on open ground. */
static void UseSomaria(void) {
    s32 x, y;
    Entity* block;
    PointInFront(16, &x, &y);
    x = ((x - gRoomControls.origin_x) & ~0xf) + 8 + gRoomControls.origin_x;
    y = ((y - gRoomControls.origin_y) & ~0xf) + 8 + gRoomControls.origin_y;
    if (gBlendLink.somariaUsed || GetCollisionDataAtWorldCoords(x, y, gPlayerEntity.base.collisionLayer) != 0) {
        SoundReq(SFX_MENU_ERROR);
        return;
    }
    block = CreateObject(PUSHABLE_STATUE, 0, 0);
    if (block != NULL) {
        block->x.HALF.HI = x;
        block->y.HALF.HI = y;
        block->collisionLayer = gPlayerEntity.base.collisionLayer;
        gBlendLink.somariaUsed = 1;
        FxAt(FX_SPARKLE, x, y);
        SoundReq(SFX_SUMMON);
    }
}

static void CastMedallion(u32 m);
static void OpenMedallionGate(u32 medallion);
static s32 MedallionIndex(u32 item);

static void UseVariant(u32 item) {
    switch (item) {
        case ITEM_HOOKSHOT:
            UseHookshot();
            break;
        case ITEM_HAMMER:
            UseHammer();
            break;
        case ITEM_CANE_OF_BYRNA:
            UseByrna();
            break;
        case ITEM_MAGIC_POWDER:
            UsePowder();
            break;
        case ITEM_CANE_OF_SOMARIA:
            UseSomaria();
            break;
        case ITEM_BOMBOS:
        case ITEM_ETHER:
        case ITEM_QUAKE:
            CastMedallion(MedallionIndex(item));
            break;
        case ITEM_MAGIC_MIRROR:
            UseMirror();
            break;
    }
}

/** Starts a new item that has no button of its own (a medallion, the Mirror). */
static bool32 StartItemAction(u32 item) {
    ItemBehavior* b = CreateItem(ITEM_PACCI_CANE);
    if (b == NULL) {
        return FALSE;
    }
    b->priority = gItemDefinitions[ITEM_PACCI_CANE].priority;
    b->behaviorId = ITEM_PACCI_CANE;
    b->field_0x2[1] = 0;
    b->field_0x2[0] = item;
    return TRUE;
}

/** Link's animation for each new item, modeled on A Link to the Past's. */
static u32 ItemAnim(u32 item) {
    switch (item) {
        case ITEM_HOOKSHOT:
            return ANIM_BLEND_HOOKSHOT;
        case ITEM_HAMMER:
            return ANIM_BLEND_HAMMER;
        case ITEM_CANE_OF_BYRNA:
            return ANIM_BLEND_BYRNA;
        case ITEM_MAGIC_POWDER:
            return ANIM_BLEND_POWDER;
        case ITEM_CANE_OF_SOMARIA:
            return ANIM_BLEND_SOMARIA;
        case ITEM_BOMBOS:
            return ANIM_BLEND_BOMBOS;
        case ITEM_ETHER:
            return ANIM_BLEND_ETHER;
        case ITEM_QUAKE:
            return ANIM_BLEND_QUAKE;
        default:
            return ANIM_BLEND_MIRROR;
    }
}

/**
 * Runs a new item on the Pacci Cane's item slot: Link plays the item's own
 * animation, and the item acts on the animation's key frame.
 */
void Blend_ExecuteVariant(ItemBehavior* this, u32 index) {
    u32 item = this->field_0x2[0];
    if (this->stateID == 0) {
        StartItemAnim(this, index, ItemAnim(item));
        return;
    }
    if (item == ITEM_HOOKSHOT && gBlendLink.hookState != HOOK_NONE) {
        Entity* hook = gBlendLink.hook;
        if (hook != NULL && hook->next != NULL && hook->kind == OBJECT && hook->id == LINK_PARTNER && hook->type == 3) {
            return; // Link holds still until the hook is back
        }
        gBlendLink.hookState = HOOK_NONE; // the hook was cleared away (a room change)
        gBlendLink.hook = NULL;
    }
    if (this->playerFrame & 0x80) {
        DeleteItemBehavior(this, index);
        return;
    }
    if ((this->playerFrame & 0x40) && !this->subtimer) {
        this->subtimer = 1;
        UseVariant(item);
    }
    UpdateItemAnim(this);
}

/* ---- Medallions -------------------------------------------------------- */

static const u8 sMedallions[] = { ITEM_BOMBOS, ITEM_ETHER, ITEM_QUAKE };
static const u8 sMedallionFx[] = { FX_GIANT_EXPLOSION, FX_LIGHTNING_STRIKE, FX_ROCK_HUGE };
static const u8 sMedallionChoiceFx[] = { FX_RED_AURA, FX_BLUE_AURA, FX_GREEN_ORB };
static const u16 sMedallionSfx[] = { SFX_ITEM_BOMB_EXPLODE, SFX_SUMMON, SFX_BOSS_EXPLODE };

static s32 MedallionIndex(u32 item) {
    u32 i;
    for (i = 0; i < ARRAY_COUNT(sMedallions); i++) {
        if (sMedallions[i] == item) {
            return i;
        }
    }
    return 0;
}

static bool32 HasSword(void) {
    u32 i;
    for (i = ITEM_SMITH_SWORD; i <= ITEM_FOURSWORD; i++) {
        if (i != ITEM_ICE_ROD && HasItem(i)) {
            return TRUE;
        }
    }
    return FALSE;
}

/** Wraps a medallion index around (the game has no division routine linked in). */
static u32 WrapMedallion(u32 m) {
    while (m >= ARRAY_COUNT(sMedallions)) {
        m -= ARRAY_COUNT(sMedallions);
    }
    return m;
}

/** Index into sMedallions of the chosen medallion, or -1 if Link has none. */
static s32 ChosenMedallion(void) {
    u32 i;
    for (i = 0; i < ARRAY_COUNT(sMedallions); i++) {
        u32 m = WrapMedallion(BLEND_MEDALLION + i);
        if (HasItem(sMedallions[m])) {
            return m;
        }
    }
    return -1;
}

static const Hitbox sSpellHitbox = { 0, 0, { 0, 0, 0, 0 }, 14, 14 };

/** Type 2: an invisible strike that hits the enemy it was put on, for medallion spells. */
static void LinkSpellHit(Entity* this) {
    if (this->action == 0) {
        this->action = 1;
        this->timer = 8;
        this->spriteSettings.draw = 0;
        this->hitbox = (Hitbox*)&sSpellHitbox;
        this->flags |= ENT_COLLIDE;
        this->hurtType = 0x09; // a full spin attack
        this->damage = this->type2 ? this->type2 : 24;
        this->collisionFlags = (gPlayerEntity.base.collisionFlags + 1) | 0x20;
        this->collisionMask = gPlayerEntity.base.collisionMask;
        sub_0801766C(this);
    }
    if (this->child != NULL && this->child->next != NULL) {
        this->x.HALF.HI = this->child->x.HALF.HI;
        this->y.HALF.HI = this->child->y.HALF.HI;
    }
    if (--this->timer == 0) {
        DeleteThisEntity();
    }
}

static void CastMedallion(u32 m) {
    u32 i;
    Entity* e;
    SoundReq(sMedallionSfx[m]);
    if (sMedallions[m] == ITEM_QUAKE) {
        InitScreenShake(60, 1);
    }
    CreateFx(&gPlayerEntity.base, sMedallionChoiceFx[m], 0);
    for (i = 0; i < ARRAY_COUNT(gEntityLists); i++) {
        LinkedList* list = &gEntityLists[i];
        for (e = list->first; (u32)e != (u32)list; e = e->next) {
            Entity* hit;
            if (e->kind != ENEMY || !(e->flags & ENT_COLLIDE) || e->health == 0 || !CheckOnScreen(e)) {
                continue;
            }
            CreateFx(e, sMedallionFx[m], 0);
            hit = CreateObject(LINK_PARTNER, 2, 0);
            if (hit != NULL) {
                CopyPosition(e, hit);
                hit->child = e;
            }
        }
    }
    gBlendLink.medallionUsed = 1 + m; // which one: Kholdstare's shell melts from Bombos (1)
    OpenMedallionGate(sMedallions[m]);
}

/**
 * The medallions from A Link to the Past. L + Select chooses one, R + Select casts it.
 * Casting needs a sword and works once per room; it strikes every enemy on screen.
 */
static void UpdateMedallions(void) {
    u32 held = gBlendLink.heldKeys;
    s32 m;
    if (!(held & (L_BUTTON | R_BUTTON))) {
        return;
    }
    // Select with L or R belongs to the medallions, not to Ezlo's hints
    gInput.heldKeys &= ~SELECT_BUTTON;
    gInput.newKeys &= ~SELECT_BUTTON;
    if (!(gBlendLink.newKeys & SELECT_BUTTON)) {
        return;
    }
    m = ChosenMedallion();
    if (m < 0 || !IsPlayerFree()) {
        return;
    }
    if (held & L_BUTTON) {
        u32 i;
        for (i = 1; i <= ARRAY_COUNT(sMedallions); i++) {
            u32 next = WrapMedallion(m + i);
            if (HasItem(sMedallions[next])) {
                BLEND_MEDALLION = next;
                break;
            }
        }
        SoundReq(SFX_TEXTBOX_SELECT);
        CreateFx(&gPlayerEntity.base, sMedallionChoiceFx[BLEND_MEDALLION], 0);
        return;
    }
    if (!HasSword() || gBlendLink.medallionUsed || gBlendLink.bunny) {
        SoundReq(SFX_MENU_ERROR);
        return;
    }
    // Link holds the medallion up high, and the spell strikes when he does
    if (StartItemAction(sMedallions[m])) {
        gBlendLink.medallionUsed = 1;
    }
}

/* ---- Bunny ------------------------------------------------------------- */

static void UpdateBunny(void) {
    u32 bunny = IsBunny();
    if (bunny) {
        // a bunny can't swing a sword, use items or lift things
        gPlayerState.flags |= PL_DISABLE_ITEMS;
    } else if (gBlendLink.bunny) {
        gPlayerState.flags &= ~PL_DISABLE_ITEMS;
    }
    if (bunny != gBlendLink.bunny) {
        gBlendLink.bunny = bunny;
        RefreshAllPalettes();
    }
}

/* ---- Where the new items are found -------------------------------------- */

typedef struct {
    u8 area;
    u8 room;
    u8 darkWorld;
    u8 item;
    u16 x; /**< room coordinates */
    u16 y;
    u8 needs; /**< item Link must have before this one shows up, or ITEM_NONE */
    u8 kind;  /**< ground item kind: 4 lies in the open, 7 is buried (dig with the Mole Mitts) */
} Treasure;

#define TREASURE_OPEN 4
#define TREASURE_BURIED 7

/**
 * Items from A Link to the Past that lie in the world, in the spirit of where
 * that game hid them. The Dark World places need the Moon Pearl first.
 */
static const Treasure sTreasures[] = {
    // Skull Woods: the Fire Rod waits in the Dark World Minish Woods
    { AREA_MINISH_WOODS, 0, 1, ITEM_FIRE_ROD, 0x228, 0x160, ITEM_MOON_PEARL, TREASURE_OPEN },
    // the Bombos tablet stands in the ruins; its medallion is buried in the Dark World ruins
    { AREA_RUINS, 0, 1, ITEM_BOMBOS, 200, 230, ITEM_MOON_PEARL, TREASURE_BURIED },
    // Ether waits on the Dark World mountain, as on Death Mountain
    { AREA_MT_CRENEL, 0, 1, ITEM_ETHER, 0xc8, 0x60, ITEM_MOON_PEARL, TREASURE_OPEN },
    // Quake is the gift of the Dark World lake, on the same island
    { AREA_LAKE_HYLIA, 0, 1, ITEM_QUAKE, 0x148, 0x108, ITEM_MOON_PEARL, TREASURE_OPEN },
    // the Ice Rod waits on Lake Hylia's stump island in the Light World, for swimmers
    { AREA_LAKE_HYLIA, 0, 0, ITEM_ICE_ROD, 0x148, 0x108, ITEM_FLIPPERS, TREASURE_OPEN },
    // the Hookshot lies in the Dark World swamp, where the Swamp Palace stood
    { AREA_CASTOR_WILDS, 0, 1, ITEM_HOOKSHOT, 0x1a8, 0x200, ITEM_MOON_PEARL, TREASURE_OPEN },
    // the Hammer waits in the dark Eastern Hills, near the Palace of Darkness
    { AREA_HYRULE_FIELD, 3, 1, ITEM_HAMMER, 0x12c, 0xb4, ITEM_MOON_PEARL, TREASURE_OPEN },
    // the Cane of Byrna is buried high on the Dark World mountain
    { AREA_MT_CRENEL, 3, 1, ITEM_CANE_OF_BYRNA, 0x240, 0xf0, ITEM_MOON_PEARL, TREASURE_BURIED },
    // Magic Powder lies deep in the Light World woods, for whoever brings a light
    { AREA_MINISH_WOODS, 0, 0, ITEM_MAGIC_POWDER, 0x150, 0x300, ITEM_LANTERN_OFF, TREASURE_OPEN },
    // the Cane of Somaria waits past the dark falls, like Misery Mire
    { AREA_VEIL_FALLS, 0, 1, ITEM_CANE_OF_SOMARIA, 0x180, 0xf0, ITEM_MOON_PEARL, TREASURE_OPEN },
};

static void SpawnTreasures(void) {
    u32 i;
    for (i = 0; i < ARRAY_COUNT(sTreasures); i++) {
        const Treasure* t = &sTreasures[i];
        Entity* e;
        if (t->area != gRoomControls.area || t->room != gRoomControls.room || t->darkWorld != BLEND_DARK_WORLD) {
            continue;
        }
        if (HasItem(t->item) || (t->needs != ITEM_NONE && !HasItem(t->needs))) {
            continue;
        }
        e = CreateObject(GROUND_ITEM, t->item, 0);
        if (e != NULL) {
            e->timer = t->kind;
            e->x.HALF.HI = gRoomControls.origin_x + t->x;
            e->y.HALF.HI = gRoomControls.origin_y + t->y;
            e->collisionLayer = 1;
        }
    }
}

/**
 * Items that come with Minish Cap's own milestones:
 * the Magic Mirror with the Fire Element from Mt. Crenel (A Link to the Past's
 * Death Mountain), and the Moon Pearl with the Ocarina after the Fortress of Winds
 * (the third dungeon, like the Tower of Hera).
 */
static void UpdateMilestoneAwards(void) {
    u32 item = ITEM_NONE;
    if (gBlendLink.awardCooldown) {
        gBlendLink.awardCooldown--;
        return;
    }
    if (!IsPlayerFree()) {
        return;
    }
    if (HasItem(ITEM_FIRE_ELEMENT) && !HasItem(ITEM_MAGIC_MIRROR)) {
        item = ITEM_MAGIC_MIRROR;
    } else if (HasItem(ITEM_OCARINA) && !HasItem(ITEM_MOON_PEARL)) {
        item = ITEM_MOON_PEARL;
    }
    if (item != ITEM_NONE) {
        InitItemGetSequence(item, 0, 0);
        gBlendLink.awardCooldown = 120;
    }
}

/** Ezlo's words the first time Link reaches the Dark World (a new line added to his text). */
#define TEXT_EZLO_DARK_WORLD TEXT_INDEX(TEXT_EZLO, 0x71)
#define BLEND_STORY_FLAGS (gSave.fillerA[2])
#define STORY_SAW_DARK_WORLD 0x1
#define STORY_MIRE_OPEN 0x2   /**< Ether was cast at the Veil Falls cave */
#define STORY_TURTLE_OPEN 0x4 /**< Quake was cast on Mt. Crenel's summit */
#define GATE_CRYSTALS 0x80    /**< DarkDungeon gate: the seven crystals (no story bit) */
#define GATE_AGAHNIM 0x40     /**< DarkDungeon gate: Agahnim beaten atop Ganon's Tower (no story bit) */

// The cracked cave in the Eastern Hills leads to the Eastern Palace. It opens once
// Link has the Earth Element, so A Link to the Past's first dungeon follows Minish Cap's.
// Each later A Link to the Past dungeon opens the same way, behind a cracked cave
// that breaks open after a Minish Cap milestone, in A Link to the Past's order:
// the Desert Palace in the Trilby Highlands after the Fire Element (when the Magic
// Mirror comes), the Tower of Hera high on Mt. Crenel after the Ocarina (when the
// Moon Pearl comes, as in A Link to the Past after the third dungeon).
typedef struct {
    u8 item;     /**< the item that opens it */
    u8 flag;     /**< the cave's local flag in the overworld bank */
} DungeonCave;

static const DungeonCave sDungeonCaves[] = {
    { ITEM_EARTH_ELEMENT, 0x70 }, // Eastern Palace: Eastern Hills
    { ITEM_FIRE_ELEMENT, 0x90 },  // Desert Palace: Trilby Highlands
    { ITEM_OCARINA, 0x44 },       // Tower of Hera: Mt. Crenel
};

// The Dark World's A Link to the Past dungeons open only in the Dark World, each
// behind a cave in the Minish Cap area that matches its place in A Link to the Past:
// the Palace of Darkness in the Eastern Hills (the Eastern Palace's cave, once Link
// has the Moon Pearl), the Swamp Palace in the Castor Wilds swamp (with the Flippers)
// and Skull Woods in the Minish Woods (with the Fire Rod that lies there, after the
// Water Element). Their way in comes first in the cave's exit list, so elsewhere the
// cave leads where it always did.
// The later ones follow A Link to the Past too: Thieves' Town in the stranger's house in
// Hyrule Town (A Link to the Past's Village of Outcasts, reached with the Hammer), the
// Ice Palace in the Lake Hylia cave (with the Flippers, and the Fire Rod to melt its
// ice), and Misery Mire at the Veil Falls cave and Turtle Rock at Mt. Crenel's summit,
// which open as in A Link to the Past once Link casts Ether and Quake in front of them
// (MedallionGate; they stay open after that).
// Ganon's Tower stands where the Tower of Hera does, as in A Link to the Past: in the Dark
// World, Hera's cracked cave on Mt. Crenel stays shut until Link has the seven crystals,
// that is until he has beaten the seven Dark World dungeons' bosses (HasSevenCrystals).
// The Pyramid of Power, Ganon's hall, lies where Hyrule Castle stands, as in A Link to the
// Past: in the Dark World the Castle Garden's castle door leads down into it once Agahnim
// has fallen atop Ganon's Tower (in A Link to the Past Ganon then smashes a hole in the
// Pyramid's top for Link to fall through).
typedef struct {
    u8 area;  /**< the dungeon's area */
    u8 item;  /**< what Link needs besides the Moon Pearl */
    u8 item2; /**< and this too, or ITEM_NONE */
    u8 gate;  /**< STORY_* bit a medallion sets to open it, or 0 */
} DarkDungeon;

static const DarkDungeon sDarkDungeons[] = {
    { AREA_NULL_1D, ITEM_MOON_PEARL, ITEM_NONE }, // Palace of Darkness
    { AREA_NULL_1E, ITEM_FLIPPERS, ITEM_NONE },   // Swamp Palace
    { AREA_NULL_1F, ITEM_FIRE_ROD, ITEM_NONE },   // Skull Woods: the Fire Rod burns its way in
    { AREA_NULL_39, ITEM_HAMMER, ITEM_NONE },     // Thieves' Town
    { AREA_NULL_3A, ITEM_FLIPPERS, ITEM_FIRE_ROD }, // Ice Palace
    { AREA_NULL_3B, ITEM_ETHER, ITEM_NONE, STORY_MIRE_OPEN }, // Misery Mire
    { AREA_NULL_3C, ITEM_QUAKE, ITEM_CANE_OF_SOMARIA, STORY_TURTLE_OPEN }, // Turtle Rock
    { AREA_NULL_3D, ITEM_MOON_PEARL, ITEM_NONE, GATE_CRYSTALS },          // Ganon's Tower
    { AREA_NULL_3E, ITEM_MOON_PEARL, ITEM_NONE, GATE_AGAHNIM },           // the Pyramid of Power
};

// where a medallion opens a dungeon: cast it in this overworld room, in the Dark World
typedef struct {
    u8 medallion;
    u8 area;
    u8 room;
    u8 gate; /**< the STORY_* bit it sets */
} MedallionGate;

static const MedallionGate sMedallionGates[] = {
    { ITEM_ETHER, AREA_VEIL_FALLS, ROOM_VEIL_FALLS_MAIN, STORY_MIRE_OPEN },
    { ITEM_QUAKE, AREA_MT_CRENEL, ROOM_MT_CRENEL_TOP, STORY_TURTLE_OPEN },
};

static const DarkDungeon* FindDarkDungeon(u32 area) {
    u32 i;
    for (i = 0; i < ARRAY_COUNT(sDarkDungeons); i++) {
        if (sDarkDungeons[i].area == area) {
            return &sDarkDungeons[i];
        }
    }
    return NULL;
}

/**
 * Whether Link has the seven crystals: each Dark World dungeon's boss beaten, its Heart
 * Container taken (local flag 0x7f in the dungeon's bank: banks 13 to 19, the Palace of
 * Darkness to Turtle Rock; see sBossRooms).
 */
static bool32 HasSevenCrystals(void) {
    u32 bank;
    for (bank = FLAG_BANK_13; bank <= FLAG_BANK_19; bank += FLAG_BANK_14 - FLAG_BANK_13) {
        if (!CheckLocalFlagByBank(bank, 0x7f)) {
            return FALSE;
        }
    }
    return TRUE;
}

static bool32 IsGateOpen(u32 gate) {
    if (gate == GATE_CRYSTALS) {
        return HasSevenCrystals();
    }
    if (gate == GATE_AGAHNIM) {
        // Agahnim's hall atop Ganon's Tower: its Heart Container taken (see sBossRooms)
        return CheckLocalFlagByBank(FLAG_BANK_20, 0x7f);
    }
    return gate == 0 || (BLEND_STORY_FLAGS & gate);
}

static bool32 CanEnterDarkDungeon(const DarkDungeon* d) {
    return BLEND_DARK_WORLD && HasItem(ITEM_MOON_PEARL) && HasItem(d->item) &&
           (d->item2 == ITEM_NONE || HasItem(d->item2)) && IsGateOpen(d->gate);
}

/** A medallion was cast: in front of a dungeon it opens, it opens that dungeon for good. */
static void OpenMedallionGate(u32 medallion) {
    u32 i;
    for (i = 0; i < ARRAY_COUNT(sMedallionGates); i++) {
        const MedallionGate* g = &sMedallionGates[i];
        if (g->medallion == medallion && g->area == gRoomControls.area && g->room == gRoomControls.room &&
            BLEND_DARK_WORLD && !(BLEND_STORY_FLAGS & g->gate)) {
            BLEND_STORY_FLAGS |= g->gate;
            SoundReq(SFX_SECRET_BIG);
        }
    }
}

/** Whether a way out of the current room is shut: a Dark World dungeon's, away from the Dark World. */
bool32 Blend_IsTransitionClosed(const Transition* t) {
    const DarkDungeon* d;
    if (Blend_ShrineClosed(t)) {
        return TRUE;
    }
    if (!(gArea.areaMetadata & AR_IS_OVERWORLD)) {
        return FALSE;
    }
    d = FindDarkDungeon(t->area);
    return d != NULL && !CanEnterDarkDungeon(d);
}

/**
 * Whether a cracked wall stays shut: the Eastern Hills cave, which leads to the
 * Palace of Darkness in the Dark World, stays shut there until Link has the Moon Pearl,
 * and the Tower of Hera's cave on Mt. Crenel, Ganon's Tower in the Dark World, until he
 * has the seven crystals.
 */
bool32 Blend_IsCaveSealed(u32 flag) {
    if (!BLEND_DARK_WORLD) {
        return FALSE;
    }
    if (gRoomControls.area == AREA_HYRULE_FIELD && flag == 0x70) {
        return !CanEnterDarkDungeon(FindDarkDungeon(AREA_NULL_1D));
    }
    if (gRoomControls.area == AREA_MT_CRENEL && flag == 0x44) {
        return !CanEnterDarkDungeon(FindDarkDungeon(AREA_NULL_3D));
    }
    return FALSE;
}

static void UpdateStory(void) {
    u32 i;
    for (i = 0; i < ARRAY_COUNT(sDungeonCaves); i++) {
        const DungeonCave* c = &sDungeonCaves[i];
        if (HasItem(c->item) && !CheckLocalFlagByBank(FLAG_BANK_1, c->flag)) {
            SetLocalFlagByBank(FLAG_BANK_1, c->flag);
        }
    }
    if (!(BLEND_STORY_FLAGS & STORY_SAW_DARK_WORLD) && Blend_IsDarkWorldActive() && IsPlayerFree() &&
        gBlendLink.treasureSpawned >= 30) {
        BLEND_STORY_FLAGS |= STORY_SAW_DARK_WORLD;
        MessageFromTarget(TEXT_EZLO_DARK_WORLD);
    }
}

static bool32 RoomHasEnemies(void) {
    u32 i;
    Entity* e;
    for (i = 0; i < ARRAY_COUNT(gEntityLists); i++) {
        LinkedList* list = &gEntityLists[i];
        for (e = list->first; (u32)e != (u32)list; e = e->next) {
            if (e->kind == ENEMY && e->health != 0) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

// A Link to the Past's boss rooms: when every enemy in the room is gone, a Heart
// Container appears, and once Link takes it the boss stays beaten. Ganon's hall instead
// ends the game: the victory tune, a fade to white, and Minish Cap's credits.
typedef struct {
    u8 area;
    u8 room;
    u8 flag; /**< local flag in the dungeon's bank, set once the Heart Container is taken */
    u16 x;
    u16 y;
} BossRoom;

enum { BOSS_FIGHTING, BOSS_PRIZE_OUT, BOSS_DONE };

static const BossRoom sBossRooms[] = {
    { AREA_EMPTY, 8, 0x7f, 0x178, 0x170 }, // Eastern Palace: the Armos Knights
    { AREA_NULL_1B, 9, 0x7e, 0x78, 0x180 },  // Desert Palace: the Lanmolas
    { AREA_NULL_1C, 5, 0xbe, 0x118, 0x108 }, // Tower of Hera: Moldorm
    { AREA_NULL_1D, 13, 0x7f, 0x178, 0x188 }, // Palace of Darkness: the Helmasaur King
    { AREA_NULL_1E, 11, 0x7f, 0x78, 0x188 },  // Swamp Palace: Arrghus
    { AREA_NULL_1F, 7, 0x7f, 0x188, 0x178 },  // Skull Woods: Mothula
    { AREA_NULL_39, 7, 0x7f, 0x178, 0x188 },  // Thieves' Town: Blind
    { AREA_NULL_3A, 20, 0x7f, 0x178, 0x98 },  // Ice Palace: Kholdstare
    { AREA_NULL_3B, 13, 0x7f, 0x78, 0x188 },  // Misery Mire: Vitreous
    { AREA_NULL_3C, 16, 0x7f, 0x88, 0x188 },  // Turtle Rock: Trinexx
    { AREA_NULL_3D, 20, 0x7f, 0x78, 0x178 },  // Ganon's Tower: Agahnim
    { AREA_NULL_3E, 0, 0x7f, 0x178, 0x88 },   // the Pyramid of Power: Ganon (the ending)
    { AREA_NULL_3F, 9, 0x7f, 0x108, 0x108 },  // the Temple of Bonds: Ganon's Tower's arena, the Armos Knights and a Lanmola
};

#define ENDING_FADE_AT 150 /**< frames from Ganon's end to the fade to white */

/** Ganon is beaten: the game's end, then the credits (Minish Cap's staff roll). */
static void UpdateEnding(void) {
    if (gBlendLink.bossState == BOSS_FIGHTING) {
        SetLocalFlag(0x7f);
        gBlendLink.bossState = BOSS_PRIZE_OUT;
        gBlendLink.endingTimer = 0;
        SoundReq(BGM_BEAT_VAATI);
        return;
    }
    if (gBlendLink.endingTimer < ENDING_FADE_AT) {
        if (++gBlendLink.endingTimer == ENDING_FADE_AT) {
            SetPlayerControl(CONTROL_DISABLED);
            SetFade(FADE_IN_OUT | FADE_BLACK_WHITE | FADE_INSTANT, 2);
        }
        return;
    }
    if (!gFadeControl.active) {
        SetTask(TASK_STAFFROLL);
    }
}

/** A big chest in one of the blend's A Link to the Past dungeons opens only with the
 * dungeon's big key, as it did there (Minish Cap's own big chests open without one). */
bool32 Blend_IsLockedBigChest(Entity* e) {
    u32 i;
    if (e->kind != OBJECT || e->id != CHEST_SPAWNER || e->type != 4 || HasDungeonBigKey()) {
        return FALSE;
    }
    for (i = 0; i < ARRAY_COUNT(sBossRooms); i++) {
        if (sBossRooms[i].area == gRoomControls.area) {
            return TRUE;
        }
    }
    return FALSE;
}

static void UpdateBossRoom(void) {
    u32 i;
    for (i = 0; i < ARRAY_COUNT(sBossRooms); i++) {
        const BossRoom* b = &sBossRooms[i];
        Entity* e;
        if (b->area != gRoomControls.area || b->room != gRoomControls.room) {
            continue;
        }
        if (b->area == AREA_NULL_3E) {
            if (gBlendLink.bossState == BOSS_PRIZE_OUT || (!CheckLocalFlag(b->flag) && !RoomHasEnemies())) {
                UpdateEnding();
            }
            return;
        }
        if (gBlendLink.bossState == BOSS_DONE || CheckLocalFlag(b->flag)) {
            return;
        }
        if (gBlendLink.bossState == BOSS_PRIZE_OUT) {
            if (gSave.stats.maxHealth > gBlendLink.bossMaxHealth) {
                SetLocalFlag(b->flag);
                gBlendLink.bossState = BOSS_DONE;
            }
            return;
        }
        if (RoomHasEnemies()) {
            return;
        }
        e = CreateObject(GROUND_ITEM, ITEM_HEART_CONTAINER, 0);
        if (e != NULL) {
            // lying in the open, so it waits for Link instead of fading like a dropped heart
            e->timer = TREASURE_OPEN;
            e->x.HALF.HI = gRoomControls.origin_x + b->x;
            e->y.HALF.HI = gRoomControls.origin_y + b->y;
            e->collisionLayer = 1;
            gBlendLink.bossMaxHealth = gSave.stats.maxHealth;
            gBlendLink.bossState = BOSS_PRIZE_OUT;
            SoundReq(SFX_SECRET_BIG);
        }
    }
}

static void UpdateRoom(void) {
    u32 key = (gRoomControls.area << 8) | gRoomControls.room;
    if (key != gBlendLink.roomKey) {
        gBlendLink.roomKey = key;
        gBlendLink.bossState = BOSS_FIGHTING;
        gBlendLink.medallionUsed = 0;
        gBlendLink.somariaUsed = 0;
        ForgetFrozen();
        gBlendLink.treasureSpawned = 0;
    }
    // wait for the room to settle (scrolling between rooms, or a warp's fade) before
    // placing treasure, or the room load clears it
    if (gBlendLink.treasureSpawned < 30 && gPlayerState.controlMode == CONTROL_ENABLED) {
        if (++gBlendLink.treasureSpawned == 30) {
            SpawnTreasures();
        }
    }
    if (gBlendLink.treasureSpawned >= 30) {
        UpdateBossRoom();
    }
}

static void UpdateDevHooks(void) {
    if (gBlendLink.devSetDarkWorld) {
        BLEND_DARK_WORLD = gBlendLink.devSetDarkWorld - 1;
        gBlendLink.devSetDarkWorld = 0;
        RefreshAllPalettes();
    }
    if (gBlendLink.devGiveItem) {
        SetInventoryValue(gBlendLink.devGiveItem, 1);
        ForceEquipItem(gBlendLink.devGiveItem, 1);
        gBlendLink.devGiveItem = 0;
    }
    if (gBlendLink.devItemGet) {
        InitItemGetSequence(gBlendLink.devItemGet, 0, 0);
        gBlendLink.devItemGet = 0;
    }
    if (gBlendLink.devSpawnEnemy) {
        Entity* e = CreateEnemy(gBlendLink.devSpawnEnemy - 1, 0);
        gBlendLink.devSpawnEnemy = 0;
        if (e != NULL) {
            Blend_ApplyDarkWorldEnemy(e);
            e->x.HALF.HI = gPlayerEntity.base.x.HALF.HI + 44;
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
    s->itemSpriteIndex = 0;
    // Link's draw routine draws his held item itself, so the item's own draw flag stays off
    if (gPlayerState.item != NULL && gPlayerState.item->kind == PLAYER_ITEM && gPlayerState.item->next != NULL) {
        Entity* item = gPlayerState.item;
        s->itemSpriteIndex = item->spriteIndex;
        s->itemX = item->x.HALF.HI - p->x.HALF.HI;
        s->itemY = item->y.HALF.HI - p->y.HALF.HI;
        s->itemFrameIndex = item->frameIndex;
        s->itemFlipX = item->spriteSettings.flipX;
        s->itemPalette = item->palette.b.b0;
        s->itemFrameSpriteSettings = item->frameSpriteSettings;
        s->itemHurtType = (item->flags & ENT_COLLIDE) && item->hitbox != NULL ? item->hurtType : 0;
        s->itemDamage = item->damage;
        if (item->hitbox != NULL) {
            s->itemHitboxX = item->hitbox->offset_x;
            s->itemHitboxY = item->hitbox->offset_y;
            s->itemHitboxWidth = item->hitbox->width;
            s->itemHitboxHeight = item->hitbox->height;
        }
    } else {
        s->itemHurtType = 0;
    }
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

static void ShowFrame(Entity* this, u32 spriteIndex, u32 frameIndex) {
    this->spriteIndex = spriteIndex;
    this->frameIndex = frameIndex;
    if (this->frameIndex != this->lastFrameIndex) {
        this->lastFrameIndex = this->frameIndex;
        sub_080042D0(this, this->frameIndex, this->spriteIndex);
    }
}

/**
 * Makes the partner's sword hit things in this game too, using the hit settings
 * of the sword in the partner's game and the collision rules of our own sword.
 */
static void UpdatePartnerItemCollision(Entity* this, BlendPlayerSync* p) {
    if (this->hitbox == NULL || p->itemHurtType == 0) {
        this->flags &= ~ENT_COLLIDE;
        return;
    }
    this->flags |= ENT_COLLIDE;
    this->hurtType = p->itemHurtType;
    this->damage = p->itemDamage;
    this->collisionFlags = (gPlayerEntity.base.collisionFlags + 1) | 0x20;
    this->collisionMask = gPlayerEntity.base.collisionMask;
    this->hitbox->offset_x = p->itemHitboxX;
    this->hitbox->offset_y = p->itemHitboxY;
    this->hitbox->width = p->itemHitboxWidth;
    this->hitbox->height = p->itemHitboxHeight;
}

/** Type 1: the partner's held item (sword swings and the like). */
static void LinkPartnerItem(Entity* this) {
    BlendPlayerSync* p = &gBlendLink.partner;
    if (this->action == 0) {
        this->action = 1;
        this->spriteSettings.draw = 0;
        this->flags &= ~ENT_COLLIDE;
        if (AllocMutableHitbox(this) != NULL) {
            sub_0801766C(this);
        }
    }
    if (!PartnerVisibleHere() || p->itemSpriteIndex == 0) {
        this->spriteSettings.draw = 0;
        this->flags &= ~ENT_COLLIDE;
        return;
    }
    UpdatePartnerItemCollision(this, p);
    this->spriteSettings.draw = 1;
    this->x.HALF.HI = p->x + p->itemX;
    this->y.HALF.HI = p->y + p->itemY;
    this->z.HALF.HI = p->z;
    this->collisionLayer = p->collisionLayer;
    if (this->palette.b.b0 != p->itemPalette) {
        SetEntityObjPalette(this, p->itemPalette);
    }
    ShowFrame(this, p->itemSpriteIndex, p->itemFrameIndex);
    this->frameSpriteSettings = p->itemFrameSpriteSettings;
    this->spriteSettings.flipX = p->itemFlipX;
    UpdateSpriteForCollisionLayer(this);
}

/** Object function for the co-op partner: a second Link driven by the other player's game. */
void LinkPartner(Entity* this) {
    BlendPlayerSync* p = &gBlendLink.partner;
    if (this->type == 1) {
        LinkPartnerItem(this);
        return;
    }
    if (this->type == 2) {
        LinkSpellHit(this);
        return;
    }
    if (this->type == 3) {
        LinkHook(this);
        return;
    }
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
    ShowFrame(this, p->spriteIndex, p->frameIndex);
    this->frameSpriteSettings = p->frameSpriteSettings;
    this->animationState = p->animationState;
    this->spriteSettings.flipX = p->flipX;
    UpdateSpriteForCollisionLayer(this);
}

static bool32 IsPartnerObject(Entity* e, u32 type) {
    return e != NULL && e->next != NULL && e->kind == OBJECT && e->id == LINK_PARTNER && e->type == type;
}

static void UpdatePartner(void) {
    if (!gBlendLink.hostConnected) {
        return;
    }
    if (!IsPartnerObject(gBlendLink.partnerEntity, 0)) {
        gBlendLink.partnerPalette = 0;
        gBlendLink.partnerEntity = CreateObject(LINK_PARTNER, 0, 0);
    }
    if (!IsPartnerObject(gBlendLink.partnerItemEntity, 1)) {
        gBlendLink.partnerItemEntity = CreateObject(LINK_PARTNER, 1, 0);
    }
}

// A Link to the Past's crystal switches (the Tower of Hera): a hit with the sword lowers
// the raised pegs and raises the others, in every room of the dungeon. The blend draws
// each room with both colors raised (custom/gen/pegs.h, from blend/dungeons/tileset.py)
// and swaps the cells that differ; a local flag of the dungeon holds which are up.
#include "../custom/gen/pegs.h"

typedef struct {
    u8 swinging; /**< Link was swinging the sword last frame */
    u8 pad[3];
    u8 pending[64]; /**< bits of swaps whose peg waits for Link to step off before rising */
} BlendPegs;

extern u8 gBlendPegsState[];
extern u8 gBlendCoopDungeons[];
#define gBlendPegs (*(BlendPegs*)gBlendPegsState)

static const BlendPegRoom* FindPegRoom(void) {
    const BlendPegRoom* r;
    for (r = sBlendPegRooms; r->area != 0xff; r++) {
        if (r->area == gRoomControls.area && r->room == gRoomControls.room) {
            return r;
        }
    }
    return NULL;
}

/** Whether Link stands (partly) on a cell, as a tile position. */
static bool32 LinkOnCell(u32 pos) {
    s32 x = gPlayerEntity.base.x.HALF.HI - gRoomControls.origin_x - (pos & 0x3f) * 16;
    s32 y = gPlayerEntity.base.y.HALF.HI - gRoomControls.origin_y - (pos >> 6) * 16;
    return x > -6 && x < 16 + 6 && y > -2 && y < 16 + 6;
}

static void ShowPeg(const u16* swap, u32 i, u32 state) {
    // a peg rising (solid in this state, not the other) waits while Link stands on it
    if (((swap[3] >> state) & 1) && !((swap[3] >> (state ^ 1)) & 1) && LinkOnCell(swap[0])) {
        gBlendPegs.pending[i >> 3] |= 1 << (i & 7);
        return;
    }
    gBlendPegs.pending[i >> 3] &= ~(1 << (i & 7));
    SetTileByIndex(swap[1 + state], swap[0], LAYER_BOTTOM);
}

static void ShowPegs(const BlendPegRoom* r, u32 state) {
    u32 i;
    MemClear(gBlendPegs.pending, sizeof(gBlendPegs.pending));
    for (i = 0; i < r->swapCount && i < 8 * sizeof(gBlendPegs.pending); i++) {
        ShowPeg(r->swaps + i * 4, i, state);
    }
}

static void UpdatePegs(void) {
    const BlendPegRoom* r = FindPegRoom();
    MapLayer* layer;
    u32 state, i, swing;
    const u16* mark;
    if (r == NULL || (r->swapCount == 0 && r->switchCount == 0)) {
        gBlendPegs.swinging = 0;
        return;
    }
    state = CheckLocalFlag(r->flag) ? 1 : 0;
    layer = GetLayerByIndex(LAYER_BOTTOM);
    // a cell no peg rises on tells which look the room's map has: a room loads the way
    // the blend drew it, the blue pegs up (a room may have a switch and no pegs)
    mark = r->swaps;
    for (i = 0; i < r->swapCount; i++) {
        if ((r->swaps[i * 4 + 3] & 3) != 1 && (r->swaps[i * 4 + 3] & 3) != 2) {
            mark = r->swaps + i * 4;
            break;
        }
    }
    if (r->swapCount == 0) {
        // a switch and no pegs here (the Palace of Darkness's 0x2B): it flips the others
    } else if (layer->mapData[mark[0]] != mark[1 + state]) {
        ShowPegs(r, state);
    } else {
        for (i = 0; i < r->swapCount && i < 8 * sizeof(gBlendPegs.pending); i++) {
            if (gBlendPegs.pending[i >> 3] & (1 << (i & 7))) {
                ShowPeg(r->swaps + i * 4, i, state);
            }
        }
    }
    swing = gPlayerState.framestate == PL_STATE_SWORD;
    if (swing && !gBlendPegs.swinging) {
        s32 x, y;
        PointInFront(12, &x, &y);
        x -= gRoomControls.origin_x;
        y -= gRoomControls.origin_y;
        for (i = 0; i < r->switchCount; i++) {
            s32 dx = x - ((r->switches[i] & 0x3f) * 16 + 8);
            s32 dy = y - ((r->switches[i] >> 6) * 16 + 8);
            if (dx > -14 && dx < 14 && dy > -14 && dy < 14) {
                if (state) {
                    ClearLocalFlag(r->flag);
                } else {
                    SetLocalFlag(r->flag);
                }
                SoundReq(SFX_METAL_CLINK);
                ShowPegs(r, state ^ 1);
                break;
            }
        }
    }
    gBlendPegs.swinging = swing;
}

void Blend_GameUpdate(void) {
    if (gBlendLink.magic != BLEND_MAGIC) {
        MemClear(&gBlendLink, sizeof(gBlendLink));
        MemClear(gBlendPegsState, 0x100); // the pegs' and the Temple of Bonds' state
        MemClear(gBlendCoopDungeons, 0x100);
        gBlendLink.magic = BLEND_MAGIC;
    }
    // the game's own record of pressed keys loses the keys the blend hides, so keep a true one
    gBlendLink.newKeys = gInput.heldKeys & ~gBlendLink.heldKeys;
    gBlendLink.heldKeys = gInput.heldKeys;
    UpdateDevHooks();
    UpdateRoom();
    UpdatePegs();
    UpdateBunny();
    UpdateMilestoneAwards();
    UpdateStory();
    UpdateRodSwap();
    UpdateMedallions();
    UpdateByrna();
    UpdateFrozen();
    UpdateMirror();
    Blend_ShrineUpdate();
    Blend_CoopDungeonsUpdate();
    Blend_GoldenSwordUpdate();
    PublishSelf();
    UpdatePartner();
}
