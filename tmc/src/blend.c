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

#define MIRROR_HOLD_FRAMES 20

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


/**
 * The picture shown for an item on the ground or held up by Link. The new
 * items borrow Minish Cap pictures: the Mirror Shield for the Magic Mirror,
 * and the glowing Elements for the Moon Pearl and the medallions.
 */
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
        default:
            return item;
    }
}

/* ---- Fire Rod ---------------------------------------------------------- */

/**
 * The Fire Rod and Ice Rod: Link swings them like the Pacci Cane and they shoot
 * the bolt Minish Cap already had for the Mirror Shield, colored red or ice white.
 */
void ItemFireRod(ItemBehavior* this, u32 index) {
    if (this->stateID == 0) {
        this->priority |= 0xf;
        sub_08077D38(this, index);
        sub_0806F948(&gPlayerEntity.base);
        sub_08077BB8(this);
        this->subtimer = 0;
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

/** L + A or L + B swaps the rod on that button to the next rod Link owns: Pacci Cane, Fire Rod, Ice Rod. */
static void UpdateRodSwap(void) {
    u32 slot;
    u32 item;
    u32 i, j;
    if (!(gInput.heldKeys & L_BUTTON) || !IsPlayerFree()) {
        return;
    }
    if (gInput.newKeys & A_BUTTON) {
        slot = SLOT_A;
    } else if (gInput.newKeys & B_BUTTON) {
        slot = SLOT_B;
    } else {
        return;
    }
    item = gSave.stats.equipped[slot];
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
    // the swap uses this press, so the rod doesn't also swing
    gInput.newKeys &= ~(A_BUTTON | B_BUTTON);
}

/* ---- Medallions -------------------------------------------------------- */

static const u8 sMedallions[] = { ITEM_BOMBOS, ITEM_ETHER, ITEM_QUAKE };
static const u8 sMedallionFx[] = { FX_GIANT_EXPLOSION, FX_LIGHTNING_STRIKE, FX_ROCK_HUGE };
static const u8 sMedallionChoiceFx[] = { FX_RED_AURA, FX_BLUE_AURA, FX_GREEN_ORB };
static const u16 sMedallionSfx[] = { SFX_ITEM_BOMB_EXPLODE, SFX_SUMMON, SFX_BOSS_EXPLODE };

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
        this->damage = 24;
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
    gBlendLink.medallionUsed = 1;
}

/**
 * The medallions from A Link to the Past. L + Select chooses one, R + Select casts it.
 * Casting needs a sword and works once per room; it strikes every enemy on screen.
 */
static void UpdateMedallions(void) {
    u32 held = gInput.heldKeys;
    s32 m;
    if (!(held & (L_BUTTON | R_BUTTON))) {
        return;
    }
    // Select with L or R belongs to the medallions, not to Ezlo's hints
    gInput.heldKeys &= ~SELECT_BUTTON;
    if (!(gInput.newKeys & SELECT_BUTTON)) {
        return;
    }
    gInput.newKeys &= ~SELECT_BUTTON;
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
    CastMedallion(m);
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
    { AREA_MINISH_WOODS, 0, 1, ITEM_FIRE_ROD, 0x1a8, 0x1f8, ITEM_MOON_PEARL, TREASURE_OPEN },
    // the Bombos tablet stands in the ruins; its medallion is buried in the Dark World ruins
    { AREA_RUINS, 0, 1, ITEM_BOMBOS, 200, 230, ITEM_MOON_PEARL, TREASURE_BURIED },
    // Ether waits on the Dark World mountain, as on Death Mountain
    { AREA_MT_CRENEL, 0, 1, ITEM_ETHER, 0x100, 0x100, ITEM_MOON_PEARL, TREASURE_OPEN },
    // Quake is the gift of the Dark World lake
    { AREA_LAKE_HYLIA, 0, 1, ITEM_QUAKE, 0x100, 0x100, ITEM_MOON_PEARL, TREASURE_OPEN },
    // the Ice Rod waits by Lake Hylia in the Light World, for swimmers
    { AREA_LAKE_HYLIA, 0, 0, ITEM_ICE_ROD, 0x100, 0x100, ITEM_FLIPPERS, TREASURE_OPEN },
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

static void UpdateStory(void) {
    if (!(BLEND_STORY_FLAGS & STORY_SAW_DARK_WORLD) && Blend_IsDarkWorldActive() && IsPlayerFree() &&
        gBlendLink.treasureSpawned >= 30) {
        BLEND_STORY_FLAGS |= STORY_SAW_DARK_WORLD;
        MessageFromTarget(TEXT_EZLO_DARK_WORLD);
    }
}

static void UpdateRoom(void) {
    u32 key = (gRoomControls.area << 8) | gRoomControls.room;
    if (key != gBlendLink.roomKey) {
        gBlendLink.roomKey = key;
        gBlendLink.medallionUsed = 0;
        gBlendLink.treasureSpawned = 0;
    }
    // wait for the room to settle (scrolling between rooms) before placing treasure
    if (gBlendLink.treasureSpawned < 30) {
        if (++gBlendLink.treasureSpawned == 30) {
            SpawnTreasures();
        }
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

void Blend_GameUpdate(void) {
    if (gBlendLink.magic != BLEND_MAGIC) {
        MemClear(&gBlendLink, sizeof(gBlendLink));
        gBlendLink.magic = BLEND_MAGIC;
    }
    UpdateDevHooks();
    UpdateRoom();
    UpdateBunny();
    UpdateMilestoneAwards();
    UpdateStory();
    UpdateRodSwap();
    UpdateMedallions();
    UpdateMirror();
    PublishSelf();
    UpdatePartner();
}
