/**
 * @file alttpEnemies.c
 * @ingroup Enemies
 *
 * @brief A Link to the Past's enemies, brought into the blend with their own
 * behavior. The Eastern Palace: Stalfos, Eyegores, Popos, Antifairies and the Armos
 * Knights. The Dark World: Ropes, Moblins and Hinox. The Desert Palace: Leevers,
 * Beamos, Pokeys, Gibdos and the Lanmolas. The Tower of Hera: Baris, Hardhat
 * Beetles, Bumpers and Moldorm; Debirandos, Kodongos and mini Moldorms. The Dark
 * World dungeons: mini Helmasaurs, Terrorpins, Hovers, Kyamerons, Zols, Firesnakes,
 * spike blocks, Medusa statues, and the Helmasaur King, Arrghus and Mothula. The last
 * Dark World dungeons (Thieves' Town, the Ice Palace, Misery Mire, Turtle Rock):
 * Freezors, Pengators, Stalfos Knights, Wizzrobes, Laser Eyes, Chain Chomps, Zazaks,
 * Gibos, fire bars and Stalfos heads, and Blind, Kholdstare, Vitreous and Trinexx.
 *
 * Their graphics are A Link to the Past's sprite sheets (plus the frames Minish Cap's
 * motion adds), made at build time from the player's cartridge by
 * blend/enemies/alttp_enemies.py (and alttp_enemies_late.py) into sprites 155 and
 * 329-335. Every frame is a
 * one-frame animation, and these enemies pick their frames themselves.
 */
#include "enemy.h"
#include "sound.h"
#include "effects.h"
#include "physics.h"
#include "asm.h"
#include "room.h"
#include "color.h"
#include "common.h"
#include "player.h"
#include "hitbox.h"
#include "collision.h"
#include "alttpBossCam.h"
#include "../../custom/gen/alttp_enemies.h"

extern const u16 gAlttpEnemyPalettes[];
extern u8 gAlttpEnemyState[];
u32 FindFreeObjPalette(u32 paletteCount);
void SetEntityObjPalette(Entity* entity, s32 palette);
void CleanUpObjPalettes(void);

void GanonLightsReset(void);

/** A boss shows the camera where it is this frame (its body's middle). */
static void BossCamAt(s32 x, s32 y) {
    gAlttpBossCam.sumX += x;
    gAlttpBossCam.sumY += y;
    gAlttpBossCam.n++;
}

static void BossCam(Entity* e, s32 dy) {
    BossCamAt(e->x.HALF.HI, e->y.HALF.HI + dy + e->z.HALF.HI);
}

/**
 * Called by Scroll1 each frame the camera follows Link: lean it from Link halfway
 * to the boss (or the middle of the bosses), so the fight stays on screen in rooms
 * taller than it, and ease back once the boss is gone.
 */
void AlttpBossCamUpdate(s32 targetX, s32 targetY) {
    AlttpBossCam* c = &gAlttpBossCam;
    if (c->area != gRoomControls.area || c->room != gRoomControls.room) {
        GanonLightsReset();
        MemClear(c, sizeof(*c));
        c->area = gRoomControls.area;
        c->room = gRoomControls.room;
        return;
    }
    if (c->n) {
        s32 wx = (c->sumX / c->n - targetX) / 2;
        s32 wy = (c->sumY / c->n - targetY) / 2;
        c->wantX = wx < -64 ? -64 : wx > 64 ? 64 : wx;
        c->wantY = wy < -64 ? -64 : wy > 36 ? 36 : wy;
        c->hold = 8;
    } else if (c->hold) {
        c->hold--;
    } else {
        c->wantX = c->wantY = 0;
    }
    c->sumX = c->sumY = 0;
    c->n = 0;
    c->biasX += c->wantX > c->biasX ? 1 : c->wantX < c->biasX ? -1 : 0;
    c->biasY += c->wantY > c->biasY ? 1 : c->wantY < c->biasY ? -1 : 0;
}

/** An Armos Knight is 32 pixels square. */
const Hitbox gHitbox_ArmosKnight = { 0, -10, { 10, 8, 8, 10 }, 11, 11 };

typedef struct {
    /*0x00*/ Entity base;
    /*0x68*/ Entity* child;
    /*0x6c*/ u8 idx;
    /*0x6d*/ u8 enemyFlags;
    /*0x6e*/ u8 rangeX;
    /*0x6f*/ u8 rangeY;
    /*0x70*/ s16 homeX;
    /*0x72*/ s16 homeY;
    /*0x74*/ u8 face;        /**< A Link to the Past facing: 0 right, 1 left, 2 down, 3 up */
    /*0x75*/ u8 head;        /**< where a Stalfos looks, same order */
    /*0x76*/ u8 step;        /**< walk cycle counter */
    /*0x77*/ u8 slot;        /**< an Armos Knight's place in the formation */
    /*0x78*/ u8 savedHealth; /**< health at the end of the last frame, to undo blocked hits */
    /*0x79*/ u8 shield;      /**< nonzero while hits only clink off */
    /*0x7a*/ u8 pose;        /**< frames left of a squash or stretch pose */
    /*0x7b*/ u8 turns;
    /*0x7c*/ s16 targetX;
    /*0x7e*/ s16 targetY;
    /*0x80*/ u8 coerced;     /**< an Armos Knight that has reached its place in the ring */
    /*0x81*/ u8 crusher;     /**< the last Armos Knight */
    /*0x82*/ u8 burst;
    /*0x83*/ u8 cooldown;    /**< frames before a Stalfos can dodge again */
    /*0x84*/ u8 unused[4];
} AlttpEnemy;

/* ---- shared helpers ---------------------------------------------------- */

/** Minish Cap direction (0 up, 8 right, 16 down, 24 left) for an ALttP facing. */
static const u8 sFaceToDir[4] = { DirectionEast, DirectionWest, DirectionSouth, DirectionNorth };

static u32 DirToFace(u32 dir) {
    static const u8 faces[4] = { 3, 0, 2, 1 };
    return faces[((dir + 4) & 0x1f) >> 3];
}

static u32 FaceTowardLink(Entity* e) {
    return DirToFace(GetFacingDirection(e, &gPlayerEntity.base));
}

static void SetFrame(Entity* e, u32 frame) {
    if (e->frameIndex != frame || e->animIndex != frame) {
        InitAnimationForceUpdate(e, frame);
    }
}

/**
 * Gives an enemy one of the blend's own palettes, sharing a slot with others of
 * its kind (custom palette ids 0x3d0 and up; blend.c uses 0x3f0 and up).
 */
static void SetPalette(Entity* e, u32 pal) {
    u32 id = 0x3d0 + pal;
    s32 slot = FindPalette(id);
    if (slot < 0) {
        slot = FindFreeObjPalette(1);
        if ((u32)slot >= 16) {
            CleanUpObjPalettes();
            slot = FindFreeObjPalette(1);
            if ((u32)slot >= 16) {
                return;
            }
        }
        gPaletteList[slot].objPaletteId = id;
        gPaletteList[slot]._0_4 = 1;
        gPaletteList[slot]._0_0 = 3;
        gPaletteList[slot]._1 = 0;
        LoadPalettes((const u8*)&gAlttpEnemyPalettes[pal * 16], slot + 16, 1);
    }
    UnloadOBJPalette(e);
    SetEntityObjPalette(e, slot);
}

static void AlttpInit(AlttpEnemy* this, u32 pal, u32 frame) {
    // A room's own enemies come with their EntityData copied over bytes 0x78-0x87
    // (room.c), which here are savedHealth, shield, pose and the rest: kind ENEMY in
    // savedHealth and flags 0x0F in shield left every one that never lowers its shield
    // immune to blows. Those bytes start clear, as they do for enemies made in play
    // (whose parents may fill targetX and the rest before their first frame).
    if (this->savedHealth == ENEMY && this->pose == super->id) {
        MemClear(&this->savedHealth, 0x10);
    }
    SetPalette(super, pal);
    super->animIndex = 0xff;
    SetFrame(super, frame);
    this->savedHealth = super->health;
    super->action = 1;
}

/** Puts back health a blow took (the enemy shrugs it off). A blow that took all of it
 * has had the engine switch the enemy's collision off, so that goes back on too. */
static void RestoreHealth(AlttpEnemy* this) {
    if (super->health == 0) {
        COLLISION_ON(super);
    }
    super->health = this->savedHealth;
}

/** A hit that only clinks off (shield up): put the health back and skip the knockback. */
static void BlockHit(AlttpEnemy* this) {
    if (super->health < this->savedHealth) {
        RestoreHealth(this);
        super->knockbackDuration = 0;
        super->iframes = -12;
        EnqueueSFX(SFX_METAL_CLINK);
    }
}

static void AlttpOnCollision(AlttpEnemy* this, void (*const table[])()) {
    if (this->shield) {
        BlockHit(this);
    } else if (super->health < this->savedHealth) {
        EnqueueSFX(SFX_HIT);
    }
    if (super->confusedTime) {
        EnemyCreateFX(super, FX_STARS);
    }
    EnemyFunctionHandlerAfterCollision(super, table);
}

static void AlttpEndTick(AlttpEnemy* this) {
    this->savedHealth = super->health;
}

static void AlttpOnGrabbed(AlttpEnemy* this) {
    switch (super->subAction) {
        case 0:
            super->subAction = 1;
            super->gustJarTolerance = 60;
            break;
        case 1:
            sub_0806F4E8(super);
            break;
        default:
            if (sub_0806F3E4(super)) {
                GenericDeath(super);
            }
            break;
    }
}

static void AlttpGrabbed(AlttpEnemy* this) {
    if (sub_0806F520(super)) {
        AlttpOnGrabbed(this);
    }
}

/** Death with a Minish Cap enemy's item drops (the drop table is chosen by enemy id). */
static void AlttpDeath(AlttpEnemy* this, u32 dropsLike) {
    EnemyCreateDeathFX((Enemy*)this, dropsLike, 0);
}

static bool32 LinkNear(Entity* e, s32 dx, s32 dy) {
    s32 x = gPlayerEntity.base.x.HALF.HI - e->x.HALF.HI;
    s32 y = gPlayerEntity.base.y.HALF.HI - e->y.HALF.HI;
    return x > -dx && x < dx && y > -dy && y < dy;
}

/* ---- Stalfos ---------------------------------------------------------------
 * Walks a few steps, stops and looks around (its head turns on its own), and
 * every fourth stop turns to Link. When Link swings his sword close by and the
 * Stalfos isn't behind him, it hops back out of reach, as in A Link to the Past.
 * Minish Cap's weight: it crouches before a hop and squashes when it lands.
 * Type 1 waits in the ceiling and drops when Link comes near.
 */
enum { ST_INIT, ST_WALK, ST_HALT, ST_CROUCH, ST_HOP, ST_LAND, ST_CEILING, ST_DROP };

static void StalfosDraw(AlttpEnemy* this) {
    u32 f;
    switch (super->action) {
        case ST_CROUCH:
            f = AF_STALFOS_CROUCH_0 + this->face * 3;
            break;
        case ST_HOP:
        case ST_DROP:
            f = AF_STALFOS_JUMP_0 + this->face * 3;
            break;
        case ST_LAND:
            f = AF_STALFOS_LAND_0 + this->face * 3;
            break;
        default:
            f = AF_STALFOS_WALK_0_0_0 + this->face * 8 + ((this->step >> 3) & 1) * 4 + this->head;
            break;
    }
    SetFrame(super, f);
}

static void StalfosStartWalk(AlttpEnemy* this) {
    static const u8 delays[4] = { 16, 32, 64, 32 };
    super->action = ST_WALK;
    super->timer = delays[Random() & 3];
    this->face = this->head;
    super->direction = sFaceToDir[this->face];
    super->speed = 0x100;
}

static void StalfosTick(AlttpEnemy* this) {
    // perpendicular turns, as in A Link to the Past (kZazak_Dir2)
    static const u8 turns[8] = { 2, 3, 2, 3, 0, 1, 0, 1 };
    switch (super->action) {
        case ST_INIT:
            AlttpInit(this, AP_STALFOS, AF_STALFOS_WALK_0_0_0);
            this->face = this->head = 2;
            super->timer = 32;
            super->action = ST_HALT;
            if (super->type != 0) {
                super->action = ST_CEILING;
                super->spriteSettings.draw = 0;
                COLLISION_OFF(super);
            }
            break;
        case ST_CEILING:
            if (LinkNear(super, 40, 40)) {
                super->action = ST_DROP;
                super->spriteSettings.draw = 1;
                super->z.HALF.HI = -112;
                super->zVelocity = 0;
                EnqueueSFX(SFX_FALL_HOLE);
            }
            return;
        case ST_DROP:
            if (GravityUpdate(super, Q_8_8(48.0)) == 0) {
                COLLISION_ON(super);
                super->action = ST_LAND;
                super->timer = 10;
                EnqueueSFX(SFX_PLY_LAND);
            }
            break;
        case ST_WALK:
            ProcessMovement0(super);
            this->step++;
            if (super->collisions != COL_NONE || --super->timer == 0) {
                super->action = ST_HALT;
                super->timer = 32;
                this->head = turns[this->face * 2 + (Random() & 1)];
                if (++this->turns == 4) {
                    this->turns = 0;
                    this->head = FaceTowardLink(super);
                    super->timer = 24;
                }
            }
            break;
        case ST_HALT:
            if (--super->timer == 0) {
                StalfosStartWalk(this);
            }
            break;
        case ST_CROUCH:
            if (--super->timer == 0) {
                super->action = ST_HOP;
                super->zVelocity = Q_16_16(2.0);
                super->speed = 0x180;
                EnqueueSFX(SFX_PLY_JUMP);
            }
            break;
        case ST_HOP:
            ProcessMovement0(super);
            if (GravityUpdate(super, Q_8_8(32.0)) == 0) {
                super->action = ST_LAND;
                super->timer = 8;
                EnqueueSFX(SFX_PLY_LAND);
            }
            break;
        case ST_LAND:
            if (--super->timer == 0) {
                super->action = ST_HALT;
                super->timer = 16;
                this->head = this->face;
            }
            break;
    }
    // dodge: Link swings his sword nearby, and the Stalfos is in front of him
    if (this->cooldown) {
        this->cooldown--;
    } else if ((super->action == ST_WALK || super->action == ST_HALT) && LinkNear(super, 40, 48) &&
        gPlayerState.framestate == PL_STATE_SWORD) {
        u32 toLink = GetFacingDirection(super, &gPlayerEntity.base);
        u32 linkFaces = gPlayerEntity.base.animationState << 2;
        // behind Link means the Stalfos looks the same way Link does
        if (((toLink - linkFaces + 4) & 0x1f) >= 8) {
            this->face = this->head = DirToFace(toLink);
            super->direction = (toLink + 16) & 0x1f;
            super->action = ST_CROUCH;
            super->timer = 4;
            this->cooldown = 40;
        }
    }
    StalfosDraw(this);
    AlttpEndTick(this);
}

static void StalfosOnCollision(AlttpEnemy* this);
static void StalfosOnDeath(AlttpEnemy* this);

static void (*const Stalfos_Functions[])(AlttpEnemy*) = {
    StalfosTick, StalfosOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    StalfosOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void StalfosOnCollision(AlttpEnemy* this) {
    AlttpOnCollision(this, (void (*const*)())Stalfos_Functions);
}

static void StalfosOnDeath(AlttpEnemy* this) {
    if (super->frameIndex != AF_STALFOS_BONES) {
        SetFrame(super, AF_STALFOS_BONES);
        EnqueueSFX(SFX_PLY_LAND);
    }
    AlttpDeath(this, STALFOS);
}

void AlttpStalfos(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Stalfos_Functions);
    EnemySetFXOffset(super, 0, 1, -20);
}

/* ---- Eyegore ---------------------------------------------------------------
 * A statue with a shut eye that blades bounce off. When Link comes close its eye
 * opens, it stomps after him for a while (only now can it be hurt), then it
 * shuts its eye and settles again. Type 1 is the red Eyegore: tougher and quicker.
 */
enum { EG_INIT, EG_WAIT, EG_OPEN, EG_CHASE, EG_CLOSE, EG_SETTLE };

static void EyegoreTick(AlttpEnemy* this) {
    static const u8 opening[8] = { 2, 2, 2, 2, 1, 1, 0, 0 };
    static const u8 closing[8] = { 0, 0, 1, 1, 2, 2, 2, 2 };
    static const u8 chase[16] = { 7, 5, 2, 9, 8, 6, 3, 10, 7, 5, 2, 9, 8, 6, 4, 11 };
    static const u8 chaseTime[4] = { 0x60, 0x80, 0xa0, 0x80 };
    u32 g = 0;
    switch (super->action) {
        case EG_INIT:
            AlttpInit(this, super->type ? AP_EYEGORE_RED : AP_EYEGORE_GREEN, AF_EYEGORE_0);
            if (super->type) {
                super->health = 16;
                this->savedHealth = 16;
            }
            super->action = EG_WAIT;
            super->timer = 0;
            this->shield = 1;
            break;
        case EG_WAIT:
            if (super->timer) {
                super->timer--;
            } else if (LinkNear(super, 48, 48)) {
                super->action = EG_OPEN;
                super->timer = 63;
                EnqueueSFX(SFX_EM_ARMOS_ON);
            }
            break;
        case EG_OPEN:
            g = opening[super->timer >> 3];
            if (--super->timer == 0) {
                super->action = EG_CHASE;
                super->timer = chaseTime[Random() & 3];
                this->face = FaceTowardLink(super);
                this->shield = 0;
            }
            break;
        case EG_CHASE:
            if (((gRoomTransition.frameCount ^ (u32)this) & 31) == 0) {
                this->face = FaceTowardLink(super);
            }
            super->direction = sFaceToDir[this->face];
            super->speed = super->type ? 0x140 : 0x100;
            ProcessMovement0(super);
            this->step++;
            g = chase[(this->step >> 2 & 12) | this->face];
            if ((this->step & 15) == 0) {
                EnqueueSFX(SFX_PLY_LAND);
            }
            if (--super->timer == 0) {
                super->action = EG_CLOSE;
                super->timer = 63;
                this->shield = 1;
            }
            break;
        case EG_CLOSE:
            g = closing[super->timer >> 3];
            if (--super->timer == 0) {
                super->action = EG_SETTLE;
                super->timer = 8;
                EnqueueSFX(SFX_THUD_HEAVY);
            }
            break;
        case EG_SETTLE:
            if (--super->timer == 0) {
                super->action = EG_WAIT;
                super->timer = 96;
            }
            SetFrame(super, AF_EYEGORE_SETTLE);
            AlttpEndTick(this);
            return;
    }
    SetFrame(super, AF_EYEGORE_0 + g);
    AlttpEndTick(this);
}

static void EyegoreOnCollision(AlttpEnemy* this);
static void EyegoreOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, EYEGORE);
}

static void (*const Eyegore_Functions[])(AlttpEnemy*) = {
    EyegoreTick, EyegoreOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    EyegoreOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void EyegoreOnCollision(AlttpEnemy* this) {
    AlttpOnCollision(this, (void (*const*)())Eyegore_Functions);
}

void AlttpEyegore(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Eyegore_Functions);
    EnemySetFXOffset(super, 0, 1, -24);
}

/* ---- Popo ------------------------------------------------------------------
 * A jelly that quivers in place, quivers faster as it gets ready, then slides off
 * in a random direction until it bumps into something. Minish Cap's wobble: it
 * squashes and stretches between A Link to the Past's two poses.
 */
enum { PP_INIT, PP_REST, PP_READY, PP_SLIDE };

static void PopoTick(AlttpEnemy* this) {
    static const u8 wobble[8] = {
        AF_POPO_0, AF_POPO_SQUASH, AF_POPO_1, AF_POPO_STRETCH,
        AF_POPO_0_FLIP, AF_POPO_SQUASH, AF_POPO_1_FLIP, AF_POPO_STRETCH,
    };
    switch (super->action) {
        case PP_INIT:
            AlttpInit(this, AP_POPO, AF_POPO_0);
            super->gustJarFlags = 1;
            super->action = PP_REST;
            super->timer = (Random() & 63) + 16;
            break;
        case PP_REST:
            this->step++;
            if (--super->timer == 0) {
                super->action = PP_READY;
                super->timer = 105;
            }
            break;
        case PP_READY:
            this->step += 2;
            if (--super->timer == 0) {
                super->action = PP_SLIDE;
                super->timer = (Random() & 63) + 128;
                super->direction = Random() & 0x1e;
                super->speed = 0xc0;
            }
            break;
        case PP_SLIDE:
            this->step += 2;
            ProcessMovement0(super);
            if (super->collisions != COL_NONE || --super->timer == 0) {
                super->action = PP_REST;
                super->timer = 80;
            }
            break;
    }
    SetFrame(super, wobble[(this->step >> 3) & 7]);
    AlttpEndTick(this);
}

static void PopoOnCollision(AlttpEnemy* this);
static void PopoOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, CHUCHU);
}

static void (*const Popo_Functions[])(AlttpEnemy*) = {
    PopoTick, PopoOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    PopoOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void PopoOnCollision(AlttpEnemy* this) {
    AlttpOnCollision(this, (void (*const*)())Popo_Functions);
}

void AlttpPopo(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Popo_Functions);
    EnemySetFXOffset(super, 0, 1, -14);
}

/* ---- Antifairy (Bubble) ------------------------------------------------------
 * A skull wrapped in sparks that drifts diagonally and bounces off walls forever.
 * Swords can't hurt it; touching it hurts.
 */
static void AntifairyTick(AlttpEnemy* this) {
    if (super->action == 0) {
        static const u8 diagonals[4] = { 4, 12, 20, 28 };
        AlttpInit(this, AP_ANTIFAIRY, AF_ANTIFAIRY_0);
        super->direction = diagonals[Random() & 3];
        super->speed = 0x100;
        this->shield = 1;
    }
    ProcessMovement0(super);
    if (super->collisions & (COL_EAST_ANY | COL_WEST_ANY)) {
        super->direction = (32 - super->direction) & 0x1f;
    }
    if (super->collisions & (COL_NORTH_ANY | COL_SOUTH_ANY)) {
        super->direction = (16 - super->direction) & 0x1f;
    }
    if (++this->step >= 12) {
        this->step = 0;
    }
    SetFrame(super, AF_ANTIFAIRY_0 + (this->step >> 1));
    AlttpEndTick(this);
}

static void AntifairyOnCollision(AlttpEnemy* this);
static void AntifairyOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, SPARK);
}

static void (*const Antifairy_Functions[])(AlttpEnemy*) = {
    AntifairyTick, AntifairyOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    AntifairyOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void AntifairyOnCollision(AlttpEnemy* this) {
    AlttpOnCollision(this, (void (*const*)())Antifairy_Functions);
}

void AlttpAntifairy(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Antifairy_Functions);
}

/* ---- Armos Knights -----------------------------------------------------------
 * Six statues that wake together and hop in formation, as A Link to the Past's
 * Armos coordinator steers them: they gather into a ring and wheel around it, draw
 * the ring in and let it out, line up along the back of the room, then march
 * forward together before reforming the ring and wheeling the other way. When one
 * knight is left it turns red, takes more hits, and leaps after Link to stomp
 * down on him, shaking the room.
 */
typedef struct {
    u16 roomKey;   /**< area << 8 | room the formation belongs to */
    u8 count;      /**< knights that joined */
    u8 phase;
    u16 timer;
    s8 spin;
    u8 radius;
    u16 angle;     /**< 0x200 per turn, as in A Link to the Past */
    u8 crusher;    /**< the last knight has turned red */
    u8 pad;
    s32 sumX, sumY;
    s16 cx, cy;
    s16 march;     /**< how far the line has marched toward the front */
    u16 pad2;
    s32 lastFrame;
} ArmosFormation;

#define gArmos (*(ArmosFormation*)gAlttpEnemyState)

enum { AF_WAKE, AF_GATHER, AF_SPIN, AF_SHRINK, AF_SPIN2, AF_GROW, AF_LINEUP, AF_MARCH };
enum { AK_INIT, AK_WAKE, AK_FORMATION, AK_CRUSH_AIM, AK_CRUSH_RISE, AK_CRUSH_HOVER, AK_CRUSH_SLAM };

static u32 ArmosRoomKey(void) {
    return (gRoomControls.area << 8) | gRoomControls.room;
}

/** The next living Armos Knight after `e` (NULL to start), in any entity list. */
static AlttpEnemy* NextKnight(Entity* e) {
    u32 i = 0;
    if (e != NULL) {
        for (i = 0; i < ARRAY_COUNT(gEntityLists); i++) {
            Entity* x;
            LinkedList* list = &gEntityLists[i];
            for (x = list->first; (u32)x != (u32)list; x = x->next) {
                if (x == e) {
                    goto found;
                }
            }
        }
        return NULL;
    found:
        e = e->next;
    }
    for (; i < ARRAY_COUNT(gEntityLists); i++) {
        LinkedList* list = &gEntityLists[i];
        if (e == NULL) {
            e = list->first;
        }
        for (; (u32)e != (u32)list; e = e->next) {
            if (e->kind == ENEMY && e->id == ARMOS_KNIGHT && e->health != 0) {
                return (AlttpEnemy*)e;
            }
        }
        e = NULL;
    }
    return NULL;
}

static u32 ArmosAlive(void) {
    u32 n = 0;
    AlttpEnemy* k;
    for (k = NextKnight(NULL); k != NULL; k = NextKnight(&k->base)) {
        n++;
    }
    return n;
}

static void ArmosCoerceAll(u32 on) {
    AlttpEnemy* k;
    for (k = NextKnight(NULL); k != NULL; k = NextKnight(&k->base)) {
        k->coerced = on;
    }
}

static bool32 ArmosAllCoerced(void) {
    AlttpEnemy* k;
    for (k = NextKnight(NULL); k != NULL; k = NextKnight(&k->base)) {
        if (!k->coerced) {
            return FALSE;
        }
    }
    return TRUE;
}

/** Where knight `slot` belongs now. */
static void ArmosTarget(AlttpEnemy* this) {
    ArmosFormation* f = &gArmos;
    if (f->phase == AF_WAKE) {
        this->targetX = super->x.HALF.HI;
        this->targetY = super->y.HALF.HI;
    } else if (f->phase >= AF_LINEUP) {
        this->targetX = f->cx - 70 + this->slot * 28;
        this->targetY = f->cy - 56 + f->march;
    } else {
        // the ring: six places 1/6 of a turn apart (kArmosCoordinator_Tab0)
        u32 a = (f->angle + (6 - this->slot) % 6 * 85) & 0x1ff;
        s32 s = gSineTable[(a >> 1) & 0xff];
        s32 c = gSineTable[((a >> 1) + 0x40) & 0xff];
        this->targetX = f->cx + ((s * f->radius) >> 8);
        this->targetY = f->cy + ((c * f->radius) >> 8);
    }
}

/** A Link to the Past's Armos coordinator, run once a frame by whichever knight goes first. */
static void ArmosCoordinator(void) {
    ArmosFormation* f = &gArmos;
    if (f->lastFrame == gRoomTransition.frameCount) {
        return;
    }
    f->lastFrame = gRoomTransition.frameCount;
    if (f->timer) {
        f->timer--;
    }
    switch (f->phase) {
        case AF_WAKE:
            if (f->timer == 0) {
                f->cx = f->sumX / f->count;
                f->cy = f->sumY / f->count;
                f->radius = 64;
                f->spin = -1;
                f->phase = AF_GATHER;
                ArmosCoerceAll(0);
            }
            return;
        case AF_GATHER:
            if (ArmosAllCoerced()) {
                f->phase = AF_SPIN;
                f->timer = 255;
            }
            break;
        case AF_SPIN:
            if (f->timer == 0) {
                f->phase = AF_SHRINK;
            }
            break;
        case AF_SHRINK:
            if (--f->radius == 32) {
                f->phase = AF_SPIN2;
                f->timer = 64;
            }
            break;
        case AF_SPIN2:
            if (f->timer == 0) {
                f->phase = AF_GROW;
            }
            break;
        case AF_GROW:
            if (++f->radius == 64) {
                f->phase = AF_LINEUP;
                f->timer = 255;
                f->march = 0;
                ArmosCoerceAll(0);
            }
            break;
        case AF_LINEUP:
            if (f->timer == 0) {
                f->phase = AF_MARCH;
            }
            return;
        case AF_MARCH:
            if (++f->march >= 112) {
                f->phase = AF_GATHER;
                f->spin = -f->spin;
                ArmosCoerceAll(0);
            }
            return;
    }
    f->angle = (f->angle + f->spin) & 0x1ff;
}

static void ArmosBecomeCrusher(AlttpEnemy* this) {
    this->crusher = 1;
    gArmos.crusher = 1;
    super->health = 16;
    super->action = AK_CRUSH_AIM;
    super->timer = 32;
    SetPalette(super, AP_ARMOS_RED);
    EnqueueSFX(SFX_EM_ARMOS_ON);
}

static void ArmosHop(AlttpEnemy* this) {
    if (GravityUpdate(super, Q_8_8(64.0)) == 0) {
        super->zVelocity = Q_16_16(3.0);
        this->pose = 6;
        EnqueueSFX(SFX_PLY_LAND);
    }
}

static void ArmosDraw(AlttpEnemy* this) {
    u32 f = AF_ARMOS_0;
    if (super->action == AK_WAKE) {
        if (super->timer < 64) {
            f = (super->timer >> 1 & 1) ? AF_ARMOS_TILT_L : AF_ARMOS_TILT_R;
        }
    } else if (this->pose) {
        this->pose--;
        f = AF_ARMOS_SQUASH;
    } else if (super->zVelocity > Q_16_16(1.0)) {
        f = AF_ARMOS_STRETCH;
    }
    SetFrame(super, f);
}

static void ArmosTick(AlttpEnemy* this) {
    ArmosFormation* f = &gArmos;
    switch (super->action) {
        case AK_INIT:
            AlttpInit(this, AP_ARMOS, AF_ARMOS_0);
            if (f->roomKey != ArmosRoomKey()) {
                MemClear(f, sizeof(*f));
                f->roomKey = ArmosRoomKey();
                f->timer = 200; // the knights wake after 160 frames, then hop in place a moment
                f->lastFrame = -1;
            }
            this->slot = f->count++;
            f->sumX += super->x.HALF.HI;
            f->sumY += super->y.HALF.HI;
            super->action = AK_WAKE;
            super->timer = 160;
            this->shield = 1;
            break;
        case AK_WAKE:
            // statues come to life: a rumble, then a shudder
            if (super->timer == 64) {
                EnqueueSFX(SFX_EM_ARMOS_ON);
            }
            if (--super->timer == 0) {
                super->action = AK_FORMATION;
                this->shield = 0;
                super->zVelocity = Q_16_16(3.0);
            }
            break;
        case AK_FORMATION:
            ArmosHop(this);
            ArmosTarget(this);
            if (this->coerced) {
                super->x.HALF.HI = this->targetX;
                super->y.HALF.HI = this->targetY;
            } else {
                s32 dx = this->targetX - super->x.HALF.HI;
                s32 dy = this->targetY - super->y.HALF.HI;
                if (dx > 0) {
                    super->x.HALF.HI++;
                } else if (dx < 0) {
                    super->x.HALF.HI--;
                }
                if (dy > 0) {
                    super->y.HALF.HI++;
                } else if (dy < 0) {
                    super->y.HALF.HI--;
                }
                if (f->phase < AF_LINEUP && dx > -3 && dx < 3 && dy > -3 && dy < 3) {
                    this->coerced = 1;
                }
            }
            if (ArmosAlive() == 1) {
                ArmosBecomeCrusher(this);
            }
            break;
        case AK_CRUSH_AIM:
            if (GravityUpdate(super, Q_8_8(64.0)) == 0 && --super->timer == 0) {
                this->targetX = gPlayerEntity.base.x.HALF.HI;
                this->targetY = gPlayerEntity.base.y.HALF.HI;
                super->direction = GetFacingDirection(super, &gPlayerEntity.base);
                super->speed = 0x200;
                super->zVelocity = Q_16_16(2.0);
                super->action = AK_CRUSH_RISE;
                EnqueueSFX(SFX_PLY_JUMP);
            }
            break;
        case AK_CRUSH_RISE: {
            s32 dx = this->targetX - super->x.HALF.HI;
            s32 dy = this->targetY - super->y.HALF.HI;
            GravityUpdate(super, Q_8_8(16.0));
            ProcessMovement0(super);
            if ((dx > -12 && dx < 12 && dy > -12 && dy < 12) || super->collisions != COL_NONE ||
                super->zVelocity < 0) {
                super->action = AK_CRUSH_HOVER;
                super->timer = 16;
            }
            break;
        }
        case AK_CRUSH_HOVER:
            if (--super->timer == 0) {
                super->action = AK_CRUSH_SLAM;
                super->zVelocity = -Q_16_16(6.5);
            }
            break;
        case AK_CRUSH_SLAM:
            if (GravityUpdate(super, Q_8_8(64.0)) == 0) {
                super->action = AK_CRUSH_AIM;
                super->timer = 32;
                this->pose = 10;
                InitScreenShake(32, 0);
                EnqueueSFX(SFX_THUD_HEAVY);
            }
            break;
    }
    if (super->action != AK_INIT) {
        ArmosCoordinator();
    }
    ArmosDraw(this);
    AlttpEndTick(this);
}

static void ArmosOnCollision(AlttpEnemy* this);

/** A beaten knight bursts apart (A Link to the Past's five burst poses), then vanishes. */
static void ArmosOnDeath(AlttpEnemy* this) {
    if (this->burst < 40) {
        if (this->burst == 0) {
            super->zVelocity = 0;
            super->z.HALF.HI = 0;
            COLLISION_OFF(super);
            EnqueueSFX(this->crusher ? SFX_BOSS_EXPLODE : SFX_ITEM_BOMB_EXPLODE);
        }
        SetFrame(super, AF_ARMOS_BURST_1 + this->burst / 8);
        this->burst++;
        return;
    }
    if (this->crusher && this->burst == 40) {
        this->burst++;
        CreateFx(super, FX_GIANT_EXPLOSION4, 0);
    }
    AlttpDeath(this, ARMOS);
}

static void (*const Armos_Functions[])(AlttpEnemy*) = {
    ArmosTick, ArmosOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    ArmosOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void ArmosOnCollision(AlttpEnemy* this) {
    AlttpOnCollision(this, (void (*const*)())Armos_Functions);
}

void ArmosKnight(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Armos_Functions);
    BossCam(super, -12);
}

/* ---- Rope -----------------------------------------------------------------
 * A snake that slithers about at a crawl, but when Link lines up with it (same row
 * or column) it coils and lunges at him at three times its crawl. Turns aside at walls.
 */
enum { RP_INIT, RP_REST, RP_CRAWL, RP_COIL };

static void RopeTick(AlttpEnemy* this) {
    // turn aside at a wall (kSpriteRope_Tab0)
    static const u8 wallTurn[4] = { 2, 3, 1, 0 };
    u32 frame;
    switch (super->action) {
        case RP_INIT:
            AlttpInit(this, AP_ROPE, AF_ROPE_0_0);
            super->gustJarFlags = 1;
            super->action = RP_REST;
            super->timer = 32;
            this->face = 2;
            break;
        case RP_REST:
            this->step++;
            if (--super->timer == 0) {
                s32 dx = gPlayerEntity.base.x.HALF.HI - super->x.HALF.HI;
                s32 dy = gPlayerEntity.base.y.HALF.HI - super->y.HALF.HI;
                u32 r = Random();
                this->face = r & 3;
                super->timer = (r & 0x7f) + 0x40;
                super->speed = 0x80;
                super->action = RP_CRAWL;
                if ((dy > -16 && dy < 16) || (dx > -16 && dx < 16)) {
                    // lined up with Link: coil, then lunge
                    this->face = FaceTowardLink(super);
                    super->action = RP_COIL;
                    super->timer = 12;
                }
            }
            break;
        case RP_COIL:
            if (--super->timer == 0) {
                super->action = RP_CRAWL;
                super->timer = 64;
                super->speed = 0x180;
                EnqueueSFX(SFX_PLY_JUMP);
            }
            break;
        case RP_CRAWL:
            super->direction = sFaceToDir[this->face];
            ProcessMovement0(super);
            this->step += super->speed > 0x100 ? 2 : 1;
            if (super->collisions != COL_NONE) {
                this->face = wallTurn[this->face];
            }
            if (--super->timer == 0) {
                super->action = RP_REST;
                super->timer = 32;
            }
            break;
    }
    if (super->action == RP_COIL) {
        frame = AF_ROPE_COIL_0 + this->face * 3;
    } else {
        frame = AF_ROPE_0_0 + this->face * 3 + ((this->step >> 3) & 1);
    }
    SetFrame(super, frame);
    AlttpEndTick(this);
}

static void RopeOnCollision(AlttpEnemy* this);
static void RopeOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, ROPE);
}

static void (*const Rope_Functions[])(AlttpEnemy*) = {
    RopeTick, RopeOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    RopeOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void RopeOnCollision(AlttpEnemy* this) {
    AlttpOnCollision(this, (void (*const*)())Rope_Functions);
}

void AlttpRope(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Rope_Functions);
    EnemySetFXOffset(super, 0, 1, -12);
}

/* ---- Moblin --------------------------------------------------------------
 * A pig-faced spearman that patrols in straight lines, turning at random and
 * every fourth turn toward Link. When it ends a walk facing Link, it rears back
 * and hurls its spear, then goes empty-handed for about a second.
 */
enum { MB_INIT, MB_PICK, MB_WALK, MB_THROW };

static void MoblinTick(AlttpEnemy* this) {
    static const u8 delays[4] = { 0x10, 0x20, 0x30, 0x40 };
    static const u8 walkPose[4] = { 6, 4, 0, 2 };
    static const u8 windUp[4] = { 11, 10, 8, 9 };
    static const u8 turns[8] = { 2, 3, 2, 3, 0, 1, 0, 1 };
    u32 g;
    switch (super->action) {
        case MB_INIT:
            AlttpInit(this, AP_MOBLIN, AF_MOBLIN_0);
            this->head = 2;
            super->action = MB_PICK;
            super->timer = 16;
            break;
        case MB_PICK:
            if (--super->timer == 0) {
                super->timer = delays[Random() & 3];
                super->action = MB_WALK;
                this->face = this->head;
                super->direction = sFaceToDir[this->face];
                super->speed = 0x100;
            }
            break;
        case MB_WALK:
            ProcessMovement0(super);
            this->step++;
            if (super->collisions == COL_NONE && --super->timer != 0) {
                break;
            }
            if (super->collisions == COL_NONE && this->face == FaceTowardLink(super)) {
                super->action = MB_THROW;
                super->timer = 32;
                break;
            }
            this->head = turns[this->face * 2 + (Random() & 1)];
            if (++this->turns == 4) {
                this->turns = 0;
                this->head = FaceTowardLink(super);
            }
            super->action = MB_PICK;
            super->timer = 12;
            break;
        case MB_THROW:
            if (super->timer == 15) {
                Entity* spear = CreateEnemy(ALTTP_SPEAR, this->face);
                if (spear != NULL) {
                    static const s8 offX[4] = { 11, -11, -3, 3 };
                    static const s8 offY[4] = { -3, -3, 3, -11 };
                    CopyPosition(super, spear);
                    spear->x.HALF.HI += offX[this->face];
                    spear->y.HALF.HI += offY[this->face];
                    this->cooldown = 64;
                    EnqueueSFX(SFX_EM_MOBLIN_SPEAR);
                }
            }
            if (--super->timer == 0) {
                super->action = MB_PICK;
                super->timer = 8;
            }
            break;
    }
    if (super->action == MB_THROW) {
        g = super->timer >= 16 ? windUp[this->face] : walkPose[this->face];
    } else {
        g = walkPose[this->face] + ((this->step >> 3) & 1);
    }
    if (this->cooldown) {
        // empty-handed while the spear flies
        this->cooldown--;
        SetFrame(super, AF_MOBLIN_BARE_0 + g);
    } else {
        SetFrame(super, AF_MOBLIN_0 + g);
    }
    AlttpEndTick(this);
}

static void MoblinOnCollision(AlttpEnemy* this);
static void MoblinOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, SPEAR_MOBLIN);
}

static void (*const Moblin_Functions[])(AlttpEnemy*) = {
    MoblinTick, MoblinOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    MoblinOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void MoblinOnCollision(AlttpEnemy* this) {
    AlttpOnCollision(this, (void (*const*)())Moblin_Functions);
}

void AlttpMoblin(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Moblin_Functions);
    EnemySetFXOffset(super, 0, 1, -20);
}

/* The Moblin's thrown spear: flies straight (type = facing) until it hits a wall. */
static void SpearTick(AlttpEnemy* this) {
    if (super->action == 0) {
        AlttpInit(this, AP_MOBLIN, AF_SPEAR_0 + (super->type & 3));
        super->direction = sFaceToDir[super->type & 3];
        super->speed = 0x200;
        super->timer = 90;
    }
    ProcessMovement0(super);
    if (super->collisions != COL_NONE || --super->timer == 0) {
        DeleteThisEntity();
    }
    AlttpEndTick(this);
}

static void SpearGone(AlttpEnemy* this) {
    DeleteThisEntity();
}

static void (*const Spear_Functions[])(AlttpEnemy*) = {
    SpearTick, SpearTick, SpearGone, SpearGone, SpearGone, SpearGone,
};

void AlttpSpear(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Spear_Functions);
}

/* =============================================================================
 * The Desert Palace, Tower of Hera and Hinox set (sprite 329)
 * ========================================================================== */

#include "gba/syscall.h"
#include "object.h"

const Hitbox gHitbox_AlttpHinox = { 0, -8, { 8, 6, 6, 8 }, 10, 12 };
const Hitbox gHitbox_AlttpTall = { 0, -8, { 6, 4, 4, 6 }, 6, 13 };
const Hitbox gHitbox_AlttpCentered = { 0, 0, { 6, 4, 4, 6 }, 7, 7 };
const Hitbox gHitbox_AlttpLaser = { 0, 0, { 3, 2, 2, 3 }, 4, 4 };
const Hitbox gHitbox_GiantMoldorm = { 0, 0, { 12, 10, 10, 12 }, 14, 14 };
const Hitbox gHitbox_HelmasaurKing = { 0, -4, { 16, 16, 16, 16 }, 28, 26 };
const Hitbox gHitbox_Arrghus = { 0, 4, { 8, 8, 8, 8 }, 12, 14 };
const Hitbox gHitbox_Mothula = { 0, 0, { 12, 6, 6, 12 }, 22, 10 };
/* A Link to the Past hits a flying boss where it is drawn: these reach up to it. */
static const Hitbox3D sHitbox_ArrghusAir = { 0, -20, { 8, 8, 8, 8 }, 12, 14, 48, { 0, 0, 0 } };
static const Hitbox3D sHitbox_MothulaAir = { 0, -24, { 12, 6, 6, 12 }, 22, 12, 56, { 0, 0, 0 } };
static const Hitbox3D sHitbox_ArrghusGround = { 0, 4, { 8, 8, 8, 8 }, 12, 14, 8, { 0, 0, 0 } };
static const Hitbox3D sHitbox_MothulaGround = { 0, 0, { 12, 6, 6, 12 }, 22, 10, 8, { 0, 0, 0 } };

/** 0-255 angle (0 north, 64 east) from one point toward another. */
static u32 AngleTo(s32 dx, s32 dy) {
    return CalculateDirectionFromOffsets(dx, dy) & 0xff;
}

/** A Link to the Past heading (16 steps, 0 east, 4 south) for a 0-255 angle. */
static u32 AngleToHeading(u32 a) {
    return ((a - 0x40 + 8) >> 4) & 15;
}

/** sin of a 0-255 angle, amplitude 256. */
static s32 Sin256(u32 a) {
    return gSineTable[a & 0xff];
}

static s32 Abs(s32 v) {
    return v < 0 ? -v : v;
}

/** Rough distance (within about 10%) without a square root. */
static s32 Distance(s32 dx, s32 dy) {
    s32 a = Abs(dx), b = Abs(dy);
    return a > b ? a + (b >> 1) - (b >> 3) : b + (a >> 1) - (a >> 3);
}

/** Pushes Link away from a point, as A Link to the Past's bumpers and Moldorm do. */
static void BounceLink(Entity* from, u32 duration) {
    Entity* link = &gPlayerEntity.base;
    link->knockbackDirection = GetFacingDirection(from, link);
    link->knockbackDuration = duration;
    link->knockbackSpeed = 0x300;
    EnqueueSFX(SFX_14C);
}

/** True while this entity's parent is still alive. */
static bool32 ParentAlive(Entity* e) {
    return e->parent != NULL && e->parent->next != NULL && e->parent->kind == ENEMY;
}

/* ---- Hinox -----------------------------------------------------------------
 * The Dark World's cyclops. It plods about in straight lines, turning at random
 * and every fourth turn to face Link (and walking faster then), and now and then
 * stops to heave a bomb over its head and hurl it. Hitting it makes it turn on
 * Link and throw at once.
 */
enum { HX_INIT, HX_PICK, HX_WALK, HX_THROW };

static void HinoxSetFace(AlttpEnemy* this, u32 face, bool32 fast) {
    this->face = face;
    super->direction = sFaceToDir[face];
    super->speed = fast ? 0x100 : 0x80;
    super->timer = (Random() & 63) + 96;
    super->action = HX_WALK;
}

static void HinoxStartThrow(AlttpEnemy* this) {
    static const s8 holdX[4] = { 8, -8, -13, 13 };
    static const s8 holdY[4] = { -11, -11, -16, -16 };
    Entity* bomb;
    super->action = HX_THROW;
    super->timer = 64;
    bomb = CreateEnemy(ALTTP_BOMB, this->face);
    if (bomb != NULL) {
        CopyPosition(super, bomb);
        bomb->x.HALF.HI += holdX[this->face];
        bomb->y.HALF.HI += 1;
        bomb->z.HALF.HI = holdY[this->face] - 1;
        bomb->parent = super;
        super->child = bomb;
    }
}

static void HinoxTick(AlttpEnemy* this) {
    static const u8 randomFaces[8] = { 2, 3, 3, 2, 0, 1, 1, 0 };
    static const u8 walkPose[4] = { 6, 4, 0, 2 };
    static const u8 throwPose[8] = { 11, 10, 8, 9, 7, 5, 1, 3 };
    u32 g;
    switch (super->action) {
        case HX_INIT:
            AlttpInit(this, AP_HINOX, AF_HINOX_0);
            this->face = 2;
            super->action = HX_PICK;
            super->timer = 32;
            break;
        case HX_PICK:
            if (this->coerced) {
                // it was hit: turn on Link and throw
                this->coerced = 0;
                this->face = FaceTowardLink(super);
                HinoxStartThrow(this);
            } else if (--super->timer == 0) {
                if ((Random() & 3) == 0) {
                    HinoxStartThrow(this);
                } else if (++this->turns == 4) {
                    this->turns = 0;
                    HinoxSetFace(this, FaceTowardLink(super), TRUE);
                } else {
                    HinoxSetFace(this, randomFaces[this->face * 2 + (Random() & 1)], FALSE);
                }
            }
            break;
        case HX_WALK:
            ProcessMovement0(super);
            if (++this->step >= 24) {
                this->step = 0;
                EnqueueSFX(SFX_PLY_LAND);
            }
            if (super->collisions != COL_NONE || --super->timer == 0 || this->coerced) {
                super->action = HX_PICK;
                super->timer = 16;
            }
            break;
        case HX_THROW:
            if (super->timer == 32 && super->child != NULL && super->child->next != NULL &&
                super->child->parent == super) {
                // let go: the bomb flies off in an arc
                static const s8 throwDir[4] = { DirectionEast, DirectionWest, DirectionSouth, DirectionNorth };
                Entity* bomb = super->child;
                bomb->parent = NULL;
                bomb->direction = throwDir[this->face];
                bomb->speed = 0x180;
                bomb->zVelocity = Q_16_16(2.5);
                bomb->action = 2;
                this->pose = 8;
                EnqueueSFX(SFX_PLY_VO4);
            }
            if (--super->timer == 0) {
                super->child = NULL;
                super->action = HX_PICK;
                super->timer = 2;
            }
            break;
    }
    if (super->action == HX_THROW) {
        g = throwPose[this->face + (super->timer <= 32 ? 4 : 0)];
        SetFrame(super, this->pose ? AF_HINOX_SQUASH : AF_HINOX_0 + g);
        if (this->pose) {
            this->pose--;
        }
    } else {
        g = walkPose[this->face] + (this->step >= 12);
        SetFrame(super, AF_HINOX_0 + g);
    }
    AlttpEndTick(this);
}

static void HinoxOnCollision(AlttpEnemy* this);
static void HinoxOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, SPEAR_MOBLIN);
}

static void (*const Hinox_Functions[])(AlttpEnemy*) = {
    HinoxTick, HinoxOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    HinoxOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void HinoxOnCollision(AlttpEnemy* this) {
    if (super->health < this->savedHealth && super->action != HX_THROW) {
        this->coerced = 1;
    }
    AlttpOnCollision(this, (void (*const*)())Hinox_Functions);
}

void AlttpHinox(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Hinox_Functions);
    EnemySetFXOffset(super, 0, 1, -20);
}

/* The Hinox's bomb (A Link to the Past's, from the shared sheet): held overhead, then
 * thrown in an arc. It swells and flickers as the fuse runs down and blows up in
 * Minish Cap's bomb blast, which hurts Link and enemies alike. Health 0 and no
 * collision: it never holds a room's doors shut. */
void AlttpBomb(AlttpEnemy* this) {
    Entity* parent = super->parent;
    switch (super->action) {
        case 0:
            EnemyInit((Enemy*)this);
            AlttpInit(this, AP_BOMB, AF_BOMB_0);
            COLLISION_OFF(super);
            super->health = 0;
            super->spritePriority.b1 = 3;
            super->timer = 110;
            break;
        case 1:
            // held over the Hinox's head until it lets go
            if (parent == NULL || parent->next == NULL) {
                super->action = 2;
            }
            break;
        case 2:
            ProcessMovement0(super);
            if (GravityUpdate(super, Q_8_8(32.0)) == 0) {
                super->action = 3;
                super->speed = 0;
                EnqueueSFX(SFX_PLACE_OBJ);
            }
            break;
    }
    if (super->action >= 2) {
        if (--super->timer == 0) {
            Entity* blast = CreateObjectWithParent(super, SMOKE_PARTICLE, 0, 0);
            if (blast != NULL) {
                blast->collisionLayer = super->collisionLayer;
            }
            DeleteThisEntity();
        }
        SetFrame(super, super->timer < 40 && (super->timer & 4) ? AF_BOMB_SWELL : AF_BOMB_0);
    }
}

/* ---- Lanmolas ----------------------------------------------------------------
 * The Desert Palace's boss: sandworms that burst from a mound in the floor, arc
 * through the air toward a point in the room, and dive back into the sand in a
 * spray of dirt, their bodies following the head's path in and out. Only the
 * head can be hit, and only while it is out of the sand. When one is beaten its
 * body blows apart segment by segment from the tail.
 */
#define LANMOLA_SEGMENTS 7
#define LANMOLA_TRAIL 64
#define LANMOLA_HEIGHT 32

typedef struct {
    s16 x, y;
    s8 z;      /**< height above the sand (negative is up) */
    u8 heading;
    u8 hidden; /**< under the sand */
    u8 pad;
} LanmolaPoint;

typedef struct {
    u16 roomKey;
    u8 count;
    u8 pad;
} LanmolaRoom;

#define gLanmolaRoom (*(LanmolaRoom*)(gAlttpEnemyState + 0xf0))
#define LanmolaTrail(slot) ((LanmolaPoint*)(gAlttpEnemyState + 0x100) + (slot) * LANMOLA_TRAIL)

enum { LM_INIT, LM_UNDER, LM_MOUND, LM_FLY, LM_SPRAY };

static void LanmolaRecord(AlttpEnemy* this, bool32 hidden) {
    LanmolaPoint* p = &LanmolaTrail(this->slot)[this->step & (LANMOLA_TRAIL - 1)];
    p->x = super->x.HALF.HI;
    p->y = super->y.HALF.HI;
    p->z = super->z.HALF.HI;
    p->heading = this->face;
    p->hidden = hidden;
    this->step++;
}

/** Picks the next place to surface and the place to dive to: near where the worm was
 * placed (its arena), and inside the room. */
static void LanmolaPickTarget(AlttpEnemy* this) {
    s16* anchor = (s16*)this->unused;
    s32 x = anchor[0] + (s32)((Random() & 0xff) * 160 >> 8) - 80;
    s32 y = anchor[1] + (s32)((Random() & 0xff) * 112 >> 8) - 56;
    s32 x0 = gRoomControls.origin_x + 32, x1 = gRoomControls.origin_x + gRoomControls.width - 32;
    s32 y0 = gRoomControls.origin_y + 40, y1 = gRoomControls.origin_y + gRoomControls.height - 32;
    this->targetX = x < x0 ? x0 : x > x1 ? x1 : x;
    this->targetY = y < y0 ? y0 : y > y1 ? y1 : y;
}

static void LanmolaSetVisible(AlttpEnemy* this, bool32 on) {
    super->spriteSettings.draw = on;
    if (on) {
        COLLISION_ON(super);
    } else {
        COLLISION_OFF(super);
    }
}

static void LanmolaTick(AlttpEnemy* this) {
    static const u8 moundPose[16] = { 4, 5, 4, 5, 4, 5, 4, 5, 4, 3, 2, 2, 1, 1, 0, 0 };
    LanmolaRoom* room = &gLanmolaRoom;
    switch (super->action) {
        case LM_INIT: {
            u32 i;
            u32 key = (gRoomControls.area << 8) | gRoomControls.room;
            AlttpInit(this, AP_LANMOLA, AF_LMDIRT_MOUND_0);
            ((s16*)this->unused)[0] = super->x.HALF.HI;
            ((s16*)this->unused)[1] = super->y.HALF.HI;
            if (room->roomKey != key) {
                room->roomKey = key;
                room->count = 0;
            }
            // three trails in the shared state; a fourth worm shares the last
            this->slot = room->count < 3 ? room->count : 2;
            room->count++;
            for (i = 0; i < LANMOLA_TRAIL; i++) {
                LanmolaTrail(this->slot)[i].hidden = 1;
            }
            for (i = 1; i <= LANMOLA_SEGMENTS; i++) {
                Entity* seg = CreateEnemy(LANMOLA_SEGMENT, i);
                if (seg != NULL) {
                    seg->parent = super;
                    CopyPosition(super, seg);
                }
            }
            super->action = LM_UNDER;
            super->timer = 40 + this->slot * 70;
            super->spriteSettings.shadow = 0;
            LanmolaSetVisible(this, FALSE);
            break;
        }
        case LM_UNDER:
            LanmolaRecord(this, TRUE);
            if (--super->timer == 0) {
                super->action = LM_MOUND;
                super->timer = 127;
                super->spriteSettings.draw = 1;
                EnqueueSFX(SFX_PLACE_OBJ);
            }
            break;
        case LM_MOUND:
            LanmolaRecord(this, TRUE);
            SetPalette(super, AP_LANMOLA_DIRT);
            SetFrame(super, AF_LMDIRT_MOUND_0 + moundPose[super->timer >> 3]);
            if (--super->timer == 0) {
                // burst out of the sand toward a point in the room
                s32 d;
                this->homeX = super->x.HALF.HI;
                this->homeY = super->y.HALF.HI;
                LanmolaPickTarget(this);
                d = Distance(this->targetX - this->homeX, this->targetY - this->homeY);
                if (d < 64) {
                    d = 64;
                } else if (d > 240) {
                    d = 240;
                }
                super->subtimer = d;
                super->timer = 0;
                super->action = LM_FLY;
                this->pose = 16;
                SetPalette(super, AP_LANMOLA);
                LanmolaSetVisible(this, TRUE);
                super->spriteSettings.shadow = 2;
                InitScreenShake(12, 0);
                EnqueueSFX(SFX_12B);
            }
            break;
        case LM_FLY: {
            // straight across the room in subtimer frames, arcing up and down
            u32 t = ++super->timer;
            u32 T = super->subtimer;
            u32 phase = Div(t * 128, T);
            s32 dx = this->targetX - this->homeX;
            s32 dy = this->targetY - this->homeY;
            s32 vx, vy, vz;
            super->x.HALF.HI = this->homeX + Div(dx * (s32)t, T);
            super->y.HALF.HI = this->homeY + Div(dy * (s32)t, T);
            super->z.HALF.HI = -(Sin256(phase) * LANMOLA_HEIGHT >> 8);
            // the head points the way it moves on screen
            vx = dx * 256;
            vy = dy * 256;
            vz = -(LANMOLA_HEIGHT * 804 * Sin256(phase + 64)) >> 8;
            this->face = AngleToHeading(AngleTo(vx >> 4, (vy + vz) >> 4));
            LanmolaRecord(this, FALSE);
            if (t >= T) {
                super->z.HALF.HI = 0;
                super->action = LM_SPRAY;
                super->timer = 128;
                this->homeX = super->x.HALF.HI;
                this->homeY = super->y.HALF.HI;
                LanmolaSetVisible(this, FALSE);
                super->spriteSettings.draw = 1;
                super->spriteSettings.shadow = 0;
                SetPalette(super, AP_LANMOLA_DIRT);
                EnqueueSFX(SFX_PLACE_OBJ);
            }
            break;
        }
        case LM_SPRAY:
            // dirt thrown up where it dove, while the body follows it down
            LanmolaRecord(this, TRUE);
            if (super->timer > 112) {
                SetFrame(super, AF_LMDIRT_SPRAY_0 + ((128 - super->timer) >> 2));
            } else {
                super->spriteSettings.draw = 0;
            }
            if (--super->timer == 0) {
                super->action = LM_UNDER;
                super->timer = 16;
                LanmolaPickTarget(this);
                super->x.HALF.HI = this->targetX;
                super->y.HALF.HI = this->targetY;
            }
            break;
    }
    if (super->action == LM_FLY) {
        SetFrame(super, AF_LANMOLA_HEAD_0 + this->face);
    }
    AlttpEndTick(this);
}

static void LanmolaOnCollision(AlttpEnemy* this);

/** The body blows apart from the tail (the segments watch burst), then the head. */
static void LanmolaOnDeath(AlttpEnemy* this) {
    if (this->burst == 0) {
        COLLISION_OFF(super);
        super->spriteSettings.draw = 1;
        EnqueueSFX(SFX_BOSS_HIT);
    }
    if (this->burst < (LANMOLA_SEGMENTS + 1) * 8) {
        this->burst++;
        return;
    }
    CreateFx(super, FX_GIANT_EXPLOSION4, 0);
    EnqueueSFX(SFX_BOSS_EXPLODE);
    DeleteThisEntity();
}

static void (*const Lanmola_Functions[])(AlttpEnemy*) = {
    LanmolaTick, LanmolaOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    LanmolaOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void LanmolaOnCollision(AlttpEnemy* this) {
    // no knockback: a worm in mid-leap keeps to its arc
    if (super->health < this->savedHealth) {
        EnqueueSFX(SFX_BOSS_HIT);
    }
    super->knockbackDuration = 0;
    EnemyFunctionHandlerAfterCollision(super, (void (*const*)())Lanmola_Functions);
}

void Lanmola(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Lanmola_Functions);
    BossCam(super, -8);
}

/** A body segment (type 1-7, 7 the tail) trailing the head along its recorded path. */
void LanmolaSegment(AlttpEnemy* this) {
    AlttpEnemy* head = (AlttpEnemy*)super->parent;
    LanmolaPoint* p;
    if (!ParentAlive(super)) {
        DeleteThisEntity();
    }
    if (super->action == 0) {
        super->flags |= ENT_DID_INIT;
        AlttpInit(this, AP_LANMOLA, AF_LANMOLA_BODY);
        COLLISION_OFF(super);
        super->health = 0;
        super->spritePriority.b1 = 2;
        super->spriteSettings.shadow = 0;
    }
    if (head->base.health == 0 && head->burst) {
        // beaten: segments burst one after another from the tail
        if (head->burst == (u32)(LANMOLA_SEGMENTS + 1 - super->type) * 8) {
            if (super->spriteSettings.draw) {
                CreateFx(super, FX_EXPLOSION, 0);
                EnqueueSFX(SFX_ITEM_BOMB_EXPLODE);
            }
            DeleteThisEntity();
        }
        return;
    }
    p = &LanmolaTrail(head->slot)[(head->step - 1 - super->type * 8) & (LANMOLA_TRAIL - 1)];
    super->spriteSettings.draw = !p->hidden;
    super->x.HALF.HI = p->x;
    super->y.HALF.HI = p->y;
    super->z.HALF.HI = p->z;
    super->spriteSettings.shadow = p->hidden ? 0 : 1;
    SetFrame(super, super->type == LANMOLA_SEGMENTS ? AF_LANMOLA_TAIL_0 + p->heading : AF_LANMOLA_BODY);
}

/* ---- Leever ----------------------------------------------------------------
 * Burrows toward Link under the sand, rises out of it spinning, chases him for a
 * few seconds, and sinks back down. It can only be hit while it is up.
 */
enum { LV_INIT, LV_UNDER, LV_EMERGE, LV_ATTACK, LV_SUBMERGE };

static void LeeverTick(AlttpEnemy* this) {
    static const u8 emergePose[16] = { 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 2, 1, 2, 1, 0, 0 };
    switch (super->action) {
        case LV_INIT:
            AlttpInit(this, AP_LEEVER, AF_LEEVER_0);
            super->action = LV_UNDER;
            super->timer = (Random() & 63) + 32;
            super->spriteSettings.draw = 0;
            COLLISION_OFF(super);
            break;
        case LV_UNDER:
            super->direction = GetFacingDirection(super, &gPlayerEntity.base);
            super->speed = 0x100;
            ProcessMovement0(super);
            if (--super->timer == 0) {
                super->action = LV_EMERGE;
                super->timer = 127;
                super->spriteSettings.draw = 1;
                EnqueueSFX(SFX_PLACE_OBJ);
            }
            break;
        case LV_EMERGE:
            SetFrame(super, AF_LEEVER_0 + emergePose[super->timer >> 3]);
            if (super->timer == 40) {
                COLLISION_ON(super);
            }
            if (--super->timer == 0) {
                super->action = LV_ATTACK;
                super->timer = (Random() & 63) + 160;
                super->speed = 0xc0;
            }
            break;
        case LV_ATTACK:
            if ((this->step & 7) == 0) {
                super->direction = GetFacingDirection(super, &gPlayerEntity.base);
            }
            ProcessMovement0(super);
            this->step++;
            SetFrame(super, AF_LEEVER_9 + ((this->step >> 2) & 3));
            if (super->collisions != COL_NONE || --super->timer == 0) {
                super->action = LV_SUBMERGE;
                super->timer = 127;
            }
            break;
        case LV_SUBMERGE:
            SetFrame(super, AF_LEEVER_0 + emergePose[(super->timer >> 3) ^ 15]);
            if (super->timer == 88) {
                COLLISION_OFF(super);
            }
            if (--super->timer == 0) {
                super->action = LV_UNDER;
                super->timer = (Random() & 31) + 64;
                super->spriteSettings.draw = 0;
            }
            break;
    }
    AlttpEndTick(this);
}

static void LeeverOnCollision(AlttpEnemy* this);
static void LeeverOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, LEEVER);
}

static void (*const Leever_Functions[])(AlttpEnemy*) = {
    LeeverTick, LeeverOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    LeeverOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void LeeverOnCollision(AlttpEnemy* this) {
    AlttpOnCollision(this, (void (*const*)())Leever_Functions);
}

void AlttpLeever(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Leever_Functions);
}

/* ---- Beamos ----------------------------------------------------------------
 * A statue whose eye sweeps round and round. When the eye falls on Link it flashes
 * and fires a laser at him. Swords only clink off it.
 */
enum { BM_INIT, BM_SWEEP, BM_FLASH, BM_REST };

/** Minish Cap direction the eye looks in for each of the 16 eye frames (west, south,
 * east, north, turning counterclockwise). */
static u32 BeamosEyeDir(u32 n) {
    return (24 - n * 2) & 0x1f;
}

static void BeamosTick(AlttpEnemy* this) {
    u32 n = (this->step >> 2) & 15;
    switch (super->action) {
        case BM_INIT:
            AlttpInit(this, AP_BEAMOS, AF_BEAMOS_0);
            this->shield = 1;
            this->step = Random() & 63;
            super->action = BM_SWEEP;
            break;
        case BM_SWEEP: {
            s32 diff = (s32)((GetFacingDirection(super, &gPlayerEntity.base) - BeamosEyeDir(n)) & 0x1f);
            this->step = (this->step + 1) & 63;
            if ((diff <= 1 || diff >= 31) && LinkNear(super, 136, 112)) {
                super->action = BM_FLASH;
                super->timer = 16;
                EnqueueSFX(SFX_193);
            }
            break;
        }
        case BM_FLASH:
            SetPalette(super, (super->timer >> 1 & 1) ? AP_BEAMOS_LASER : AP_BEAMOS);
            if (--super->timer == 0) {
                u32 i;
                s32 eyeX = super->x.HALF.HI + ((Sin256(BeamosEyeDir(n) * 8) * 9) >> 8);
                s32 eyeY = super->y.HALF.HI - 18 - ((Sin256(BeamosEyeDir(n) * 8 + 64) * 5) >> 8);
                u32 angle = AngleTo(gPlayerEntity.base.x.HALF.HI - eyeX, gPlayerEntity.base.y.HALF.HI - 6 - eyeY);
                SetPalette(super, AP_BEAMOS);
                // a train of bolts, one behind another
                for (i = 0; i < 3; i++) {
                    Entity* bolt = CreateEnemy(BEAMOS_LASER, angle);
                    if (bolt != NULL) {
                        bolt->x.HALF.HI = eyeX;
                        bolt->y.HALF.HI = eyeY + 6;
                        bolt->z.HALF.HI = -6;
                        bolt->collisionLayer = super->collisionLayer;
                        bolt->timer = i * 4 + 1;
                    }
                }
                EnqueueSFX(SFX_149);
                super->action = BM_REST;
                super->timer = 80;
            }
            break;
        case BM_REST:
            if (--super->timer == 0) {
                super->action = BM_SWEEP;
            }
            break;
    }
    SetFrame(super, AF_BEAMOS_0 + ((this->step >> 2) & 15));
    AlttpEndTick(this);
}

static void BeamosOnCollision(AlttpEnemy* this);
static void BeamosOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, BEETLE);
}

static void (*const Beamos_Functions[])(AlttpEnemy*) = {
    BeamosTick, BeamosOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    BeamosOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void BeamosOnCollision(AlttpEnemy* this) {
    super->knockbackDuration = 0;
    AlttpOnCollision(this, (void (*const*)())Beamos_Functions);
}

void AlttpBeamos(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Beamos_Functions);
}

/* A laser bolt: type is its 0-255 angle. Bolts wait timer frames (so three fired at
 * once fly in a line), then streak until they hit a wall or Link. */
void BeamosLaser(AlttpEnemy* this) {
    if (super->action == 0) {
        EnemyInit((Enemy*)this);
        AlttpInit(this, AP_BEAMOS_LASER, AF_BEAMLASER_0 + ((((super->type - 0x40) & 0xff) + 16) >> 5 & 7));
        super->direction = ((super->type + 4) >> 3) & 0x1f;
        super->speed = 0x500;
        super->spriteSettings.draw = 0;
        COLLISION_OFF(super);
        super->subtimer = 60;
    }
    if (super->timer) {
        if (--super->timer == 0) {
            super->spriteSettings.draw = 1;
            COLLISION_ON(super);
        }
        return;
    }
    if (super->contactFlags & CONTACT_NOW) {
        DeleteThisEntity();
    }
    ProcessMovement0(super);
    if (super->collisions != COL_NONE || --super->subtimer == 0) {
        CreateFx(super, FX_SPARKLE2, 0);
        DeleteThisEntity();
    }
}

/* ---- Pokey ---------------------------------------------------------------
 * A stack of four spiked balls hopping diagonally around the room, settling with
 * a squash each time it lands. A sword hit knocks the bottom ball loose (it bounces
 * off on its own and bursts after a few bounces) and the stack gets faster; only
 * the last ball, the head, can be beaten.
 */
enum { PK_INIT, PK_GROUND, PK_AIR };

static void PokeyBounceWalls(Entity* e) {
    if (e->collisions & (COL_EAST_ANY | COL_WEST_ANY)) {
        e->direction = (32 - e->direction) & 0x1f;
    }
    if (e->collisions & (COL_NORTH_ANY | COL_SOUTH_ANY)) {
        e->direction = (16 - e->direction) & 0x1f;
    }
}

static void PokeyTick(AlttpEnemy* this) {
    static const u8 settle[8] = { 8, 8, 6, 6, 4, 6, 6, 8 };
    u32 d = 8;
    if (super->type == 1) {
        // a ball knocked loose
        if (super->action == 0) {
            AlttpInit(this, AP_POKEY, AF_POKEY_BALL);
            super->zVelocity = Q_16_16(1.5);
            super->speed = 0x180;
        }
        ProcessMovement0(super);
        if (super->collisions != COL_NONE) {
            PokeyBounceWalls(super);
            if (++this->turns >= 3) {
                CreateFx(super, FX_DEATH, 0);
                DeleteThisEntity();
            }
            EnqueueSFX(SFX_14C);
        }
        if (GravityUpdate(super, Q_8_8(32.0)) == 0) {
            super->zVelocity = Q_16_16(1.0);
        }
        AlttpEndTick(this);
        return;
    }
    switch (super->action) {
        case PK_INIT: {
            static const u8 diagonals[4] = { 4, 12, 20, 28 };
            AlttpInit(this, AP_POKEY, AF_POKEY_4_8);
            this->turns = 4; // balls in the stack
            super->direction = diagonals[Random() & 3];
            super->speed = 0x100;
            super->action = PK_GROUND;
            super->timer = 15;
            break;
        }
        case PK_GROUND:
            d = settle[super->timer >> 1];
            if (--super->timer == 0) {
                super->action = PK_AIR;
                super->zVelocity = Q_16_16(1.0);
            }
            break;
        case PK_AIR:
            ProcessMovement0(super);
            PokeyBounceWalls(super);
            if (GravityUpdate(super, Q_8_8(32.0)) == 0) {
                super->action = PK_GROUND;
                super->timer = 15;
            }
            break;
    }
    SetFrame(super, AF_POKEY_1_8 + (this->turns - 1) * 3 + (d == 8 ? 0 : d == 6 ? 1 : 2));
    AlttpEndTick(this);
}

static void PokeyOnCollision(AlttpEnemy* this);
static void PokeyOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, super->type ? 0 : CHUCHU);
}

static void (*const Pokey_Functions[])(AlttpEnemy*) = {
    PokeyTick, PokeyOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    PokeyOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void PokeyOnCollision(AlttpEnemy* this) {
    if (super->type == 0 && this->turns > 1 && super->health < this->savedHealth) {
        // knock the bottom ball loose; the stack itself is unhurt but quicker
        Entity* ball = CreateEnemy(ALTTP_POKEY, 1);
        if (ball != NULL) {
            CopyPosition(super, ball);
            ball->direction = super->knockbackDirection;
        }
        this->turns--;
        RestoreHealth(this);
        super->speed += 0x40;
        super->knockbackDuration = 0;
        EnqueueSFX(SFX_HIT);
    }
    AlttpOnCollision(this, (void (*const*)())Pokey_Functions);
}

void AlttpPokey(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Pokey_Functions);
}

/* ---- Gibdo ---------------------------------------------------------------
 * A mummy that shuffles straight at Link. When it has to change direction it
 * turns on the spot step by step (through the diagonals) before setting off again.
 */
enum { GB_INIT, GB_TURN, GB_WALK };

static void GibdoTick(AlttpEnemy* this) {
    static const u8 target[4] = { 2, 6, 4, 0 };       /* facing -> one of 8 turning steps */
    static const u8 turnPose[8] = { 4, 8, 11, 10, 0, 6, 3, 7 };
    static const u8 walkPose[8] = { 9, 2, 0, 4, 11, 3, 1, 5 };
    switch (super->action) {
        case GB_INIT:
            AlttpInit(this, AP_GIBDO, AF_GIBDO_0);
            this->face = FaceTowardLink(super);
            this->head = target[this->face];
            super->action = GB_WALK;
            super->timer = 48;
            super->speed = 0x80;
            break;
        case GB_TURN:
            if ((++this->step & 7) == 0) {
                s32 diff = (target[this->face] - this->head) & 7;
                if (diff == 0) {
                    super->action = GB_WALK;
                    super->timer = (Random() & 31) + 48;
                } else {
                    this->head = (this->head + (diff <= 4 ? 1 : 7)) & 7;
                }
            }
            SetFrame(super, AF_GIBDO_0 + turnPose[this->head]);
            break;
        case GB_WALK: {
            u32 face;
            super->direction = sFaceToDir[this->face];
            ProcessMovement0(super);
            if (++this->turns >= 28) {
                this->turns = 0;
                this->step ^= 1;
            }
            if ((super->timer == 0 || --super->timer == 0 || super->collisions != COL_NONE) &&
                (face = FaceTowardLink(super)) != this->face) {
                this->face = face;
                super->action = GB_TURN;
            }
            SetFrame(super, AF_GIBDO_0 + walkPose[(this->step & 1) << 2 | this->face]);
            break;
        }
    }
    AlttpEndTick(this);
}

static void GibdoOnCollision(AlttpEnemy* this);
static void GibdoOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, GOBDO);
}

static void (*const Gibdo_Functions[])(AlttpEnemy*) = {
    GibdoTick, GibdoOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    GibdoOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void GibdoOnCollision(AlttpEnemy* this) {
    AlttpOnCollision(this, (void (*const*)())Gibdo_Functions);
}

void AlttpGibdo(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Gibdo_Functions);
}

/* ---- Moldorm ---------------------------------------------------------------
 * The Tower of Hera's boss: a giant worm racing around its arena with its eyes
 * fixed ahead, bouncing off the walls. It weaves at random and every third run
 * swerves to chase Link; touching it throws Link back (off the edge, if he is
 * unlucky). Its head and body shrug off the sword: only the tail can be hurt, and
 * it gets faster as it weakens. Beaten, it bursts apart from the tail.
 */
#define MOLDORM_TRAIL 128
#define MoldormTrail ((s16*)(gAlttpEnemyState + 0x800))

enum { MD_INIT, MD_STRAIGHT, MD_MEANDER, MD_LUNGE };

static void MoldormRecord(AlttpEnemy* this) {
    u32 i = this->step & (MOLDORM_TRAIL - 1);
    MoldormTrail[i * 2] = super->x.HALF.HI;
    MoldormTrail[i * 2 + 1] = super->y.HALF.HI;
}

static void MoldormTick(AlttpEnemy* this) {
    bool32 weak = super->health < 6;
    u32 i;
    switch (super->action) {
        case MD_INIT: {
            static const u8 lookback[4] = { 1, 2, 3, 4 };
            AlttpInit(this, AP_MOLDORM, AF_MOLDORM_HEAD_0);
            this->shield = 1;
            this->face = 4;
            super->action = MD_STRAIGHT;
            super->timer = 48;
            for (i = 0; i < MOLDORM_TRAIL; i++) {
                MoldormTrail[i * 2] = super->x.HALF.HI;
                MoldormTrail[i * 2 + 1] = super->y.HALF.HI;
            }
            for (i = 0; i < 4; i++) {
                Entity* seg = CreateEnemy(MOLDORM_SEGMENT, lookback[i] - 1);
                if (seg != NULL) {
                    seg->parent = super;
                    CopyPosition(super, seg);
                }
            }
            break;
        }
        case MD_STRAIGHT:
            if (--super->timer == 0) {
                if (++this->turns == 3) {
                    this->turns = 0;
                    super->action = MD_LUNGE;
                } else {
                    super->action = MD_MEANDER;
                }
                this->head = (Random() & 2) ? 1 : 15; // which way it weaves
                super->timer = (Random() & 31) + 32;
            }
            break;
        case MD_MEANDER:
            if (--super->timer == 0) {
                super->timer = (Random() & 15) + 8;
                super->action = MD_STRAIGHT;
            } else if ((super->timer & 3) == 0) {
                this->face = (this->face + this->head) & 15;
            }
            break;
        case MD_LUNGE:
            if ((++this->cooldown & 3) == 0) {
                u32 want = AngleToHeading(AngleTo(gPlayerEntity.base.x.HALF.HI - super->x.HALF.HI,
                                                  gPlayerEntity.base.y.HALF.HI - super->y.HALF.HI));
                s32 diff = (want - this->face) & 15;
                if (diff == 0) {
                    super->action = MD_STRAIGHT;
                    super->timer = 48;
                } else {
                    this->face = (this->face + (diff < 8 ? 1 : 15)) & 15;
                }
            }
            break;
    }
    super->direction = (this->face * 2 + 8) & 0x1f;
    super->speed = weak ? 0x240 : 0x180;
    ProcessMovement0(super);
    if (super->collisions != COL_NONE) {
        this->face = (this->face + 8) & 15;
        EnqueueSFX(SFX_14C);
    }
    // the trail the body follows, a step (or two, when weak) per frame
    this->step++;
    MoldormRecord(this);
    if (weak) {
        this->step++;
        MoldormRecord(this);
    }
    if ((this->step & (weak ? 7 : 15)) == 0) {
        EnqueueSFX(SFX_12B);
    }
    if (super->contactFlags & CONTACT_NOW) {
        BounceLink(super, 16);
    }
    SetFrame(super, AF_MOLDORM_HEAD_0 + this->face);
    AlttpEndTick(this);
}

static void MoldormOnCollision(AlttpEnemy* this);

static void MoldormOnDeath(AlttpEnemy* this) {
    if (this->burst == 0) {
        COLLISION_OFF(super);
        EnqueueSFX(SFX_BOSS_HIT);
    }
    if (this->burst < 5 * 16) {
        this->burst++;
        SetFrame(super, AF_MOLDORM_HEAD_0 + ((this->face + (this->burst >> 1)) & 15));
        return;
    }
    CreateFx(super, FX_GIANT_EXPLOSION4, 0);
    EnqueueSFX(SFX_BOSS_EXPLODE);
    DeleteThisEntity();
}

static void (*const Moldorm_Functions[])(AlttpEnemy*) = {
    MoldormTick, MoldormOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    MoldormOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void MoldormOnCollision(AlttpEnemy* this) {
    super->knockbackDuration = 0;
    AlttpOnCollision(this, (void (*const*)())Moldorm_Functions);
}

void GiantMoldorm(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Moldorm_Functions);
    BossCam(super, -8);
}

/** Moldorm's body: type 0 and 1 the big segments, 2 the small one, 3 the tail. Only
 * the tail takes hits, and passes them on to the head. */
void MoldormSegment(AlttpEnemy* this) {
    static const u8 lookback[4] = { 16, 28, 40, 48 };
    AlttpEnemy* head = (AlttpEnemy*)super->parent;
    u32 i, spin;
    if (!ParentAlive(super)) {
        DeleteThisEntity();
    }
    if (super->action == 0) {
        super->flags |= ENT_DID_INIT;
        AlttpInit(this, super->type == 3 ? AP_MOLDORM_TAIL : AP_MOLDORM, AF_MOLDORM_SEG_0);
        super->spritePriority.b1 = 2;
        if (super->type == 3) {
            super->hitbox = (Hitbox*)&gHitbox_AlttpCentered;
            this->savedHealth = super->health = 255;
        } else {
            COLLISION_OFF(super);
            super->health = 0;
        }
    }
    if (head->base.health == 0) {
        // burst apart, tail first
        if (head->burst == (u32)(4 - super->type) * 16) {
            CreateFx(super, FX_GIANT_EXPLOSION4, 0);
            EnqueueSFX(SFX_ITEM_BOMB_EXPLODE);
            DeleteThisEntity();
        }
        COLLISION_OFF(super);
        return;
    }
    if (super->type == 3 && super->health < this->savedHealth) {
        // a hit on the tail hurts the head
        u32 dmg = this->savedHealth - super->health;
        head->base.health = head->base.health > dmg ? head->base.health - dmg : 0;
        head->base.iframes = 30;
        COLLISION_ON(super);
        super->health = this->savedHealth = 255;
        super->knockbackDuration = 0;
        EnqueueSFX(SFX_BOSS_HIT);
    }
    i = (head->step - lookback[super->type]) & (MOLDORM_TRAIL - 1);
    super->x.HALF.HI = MoldormTrail[i * 2];
    super->y.HALF.HI = MoldormTrail[i * 2 + 1];
    spin = (head->step >> 1) & 3;
    if (super->type < 2) {
        SetFrame(super, AF_MOLDORM_SEG_0 + (spin & 1));
    } else if (super->type == 2) {
        SetFrame(super, AF_MOLDORM_SMALL_0 + spin);
    } else {
        SetFrame(super, AF_MOLDTAIL_0 + spin);
    }
}

/* ---- Bari --------------------------------------------------------------------
 * A floating jellyfish drifting in lazy curves. Every so often it crackles with
 * electricity; then the sword only clinks off it. Red Baris split into two small
 * ones when beaten. Type 0 blue, 1 red, 2 a small one.
 */
enum { BR_INIT, BR_DRIFT, BR_SHOCK };

static void BariTick(AlttpEnemy* this) {
    u32 f;
    switch (super->action) {
        case BR_INIT:
            AlttpInit(this, super->type == 0 ? AP_BARI_BLUE : AP_BARI_RED,
                      super->type == 2 ? AF_BARI_SMALL_0 : AF_BARI_0);
            this->face = Random() & 15;
            this->cooldown = (Random() & 63) + 128;
            super->action = BR_DRIFT;
            super->speed = super->type == 2 ? 0x60 : 0x40;
            if (super->type == 2) {
                super->speed = 0x200;
                super->timer = 8;
            }
            break;
        case BR_DRIFT:
            if (super->type != 2 && this->cooldown && --this->cooldown == 0) {
                if (Random() & 1) {
                    super->action = BR_SHOCK;
                    super->timer = 128;
                    this->shield = 1;
                    EnqueueSFX(SFX_193);
                } else {
                    this->cooldown = (Random() & 63) + 128;
                }
            }
            break;
        case BR_SHOCK:
            if (--super->timer == 0) {
                super->action = BR_DRIFT;
                this->shield = 0;
                this->cooldown = (Random() & 63) + 128;
            }
            break;
    }
    if (super->type == 2 && super->timer) {
        // flung apart from the split, then drifting
        if (--super->timer == 0) {
            super->speed = 0x60;
        }
    } else if (((this->step + super->type) & 15) == 0) {
        this->face = (this->face + ((this->turns & 1) ? 15 : 1)) & 15;
        if ((Random() & 3) == 0) {
            this->turns++;
        }
        super->direction = this->face * 2;
    }
    this->step++;
    if ((this->step & 3) == 0 || super->timer) {
        ProcessMovement0(super);
    }
    if (super->type == 2) {
        f = AF_BARI_SMALL_0 + ((this->step >> 3) & 1);
    } else if (super->action == BR_SHOCK) {
        f = (this->step & 2) ? AF_BARI_SHOCK : AF_BARI_0;
    } else {
        // Minish Cap's bob: a stretch at the top of each pulse
        static const u8 bob[4] = { AF_BARI_0, AF_BARI_1, AF_BARI_STRETCH, AF_BARI_1 };
        f = bob[(this->step >> 3) & 3];
    }
    SetFrame(super, f);
    AlttpEndTick(this);
}

static void BariOnCollision(AlttpEnemy* this);
static void BariOnDeath(AlttpEnemy* this) {
    if (super->type == 1) {
        u32 i;
        for (i = 0; i < 2; i++) {
            Entity* small = CreateEnemy(ALTTP_BARI, 2);
            if (small != NULL) {
                CopyPosition(super, small);
                small->direction = i ? DirectionEast : DirectionWest;
            }
        }
        CreateFx(super, FX_DEATH, 0);
        DeleteThisEntity();
    }
    AlttpDeath(this, super->type == 2 ? 0 : CHUCHU);
}

static void (*const Bari_Functions[])(AlttpEnemy*) = {
    BariTick, BariOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    BariOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void BariOnCollision(AlttpEnemy* this) {
    AlttpOnCollision(this, (void (*const*)())Bari_Functions);
}

void AlttpBari(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Bari_Functions);
}

/* ---- Hardhat Beetle ------------------------------------------------------------
 * A helmeted beetle that skates after Link with momentum, overshooting and
 * curving back. The sword can't hurt it, only knock it back hard (into a pit, if
 * there is one handy).
 */
static void HardhatTick(AlttpEnemy* this) {
    s16* vx = &this->targetX;
    s16* vy = &this->targetY;
    if (super->action == 0) {
        AlttpInit(this, AP_HARDHAT, AF_HARDHAT_0);
        *vx = *vy = 0;
    }
    if (((this->step++) & 31) == 0) {
        // aim again
        u32 a = AngleTo(gPlayerEntity.base.x.HALF.HI - super->x.HALF.HI,
                        gPlayerEntity.base.y.HALF.HI - super->y.HALF.HI);
        this->homeX = (Sin256(a) * 0x100) >> 8;
        this->homeY = -(Sin256(a + 64) * 0x100) >> 8;
    }
    if ((this->step & 1) == 0) {
        *vx += *vx < this->homeX ? 16 : -16;
        *vy += *vy < this->homeY ? 16 : -16;
    }
    super->direction = (AngleTo(*vx, *vy) + 4) >> 3 & 0x1f;
    super->speed = Distance(*vx, *vy);
    if (*vx || *vy) {
        ProcessMovement0(super);
    }
    if (super->collisions & (COL_EAST_ANY | COL_WEST_ANY)) {
        *vx = 0;
    }
    if (super->collisions & (COL_NORTH_ANY | COL_SOUTH_ANY)) {
        *vy = 0;
    }
    if (this->pose) {
        this->pose--;
        SetFrame(super, AF_HARDHAT_SQUASH);
    } else {
        SetFrame(super, AF_HARDHAT_0 + ((this->step >> 2) & 1));
    }
    AlttpEndTick(this);
}

static void HardhatOnCollision(AlttpEnemy* this);
static void HardhatOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, BEETLE);
}

static void (*const Hardhat_Functions[])(AlttpEnemy*) = {
    HardhatTick, HardhatOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    HardhatOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void HardhatOnCollision(AlttpEnemy* this) {
    if (super->health < this->savedHealth) {
        // unhurt, but sent flying, and it loses its momentum
        RestoreHealth(this);
        super->knockbackDuration = 16;
        super->knockbackSpeed = 0x300;
        this->targetX = this->targetY = 0;
        this->pose = 10;
        EnqueueSFX(SFX_METAL_CLINK);
    }
    EnemyFunctionHandlerAfterCollision(super, (void (*const*)())Hardhat_Functions);
}

void AlttpHardhat(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Hardhat_Functions);
}

/* ---- Bumper --------------------------------------------------------------------
 * A fixed bumper that throws Link back when he touches it, with a Minish Cap
 * overshoot and a wobble. It is scenery: health 0, so it never keeps a room's
 * doors shut.
 */
void AlttpBumper(AlttpEnemy* this) {
    if (super->action == 0) {
        EnemyInit((Enemy*)this);
        AlttpInit(this, AP_BUMPER, AF_BUMPER_0);
        COLLISION_OFF(super);
        super->health = 0;
        super->spriteSettings.shadow = 0;
        super->spritePriority.b1 = 3;
    }
    if (super->timer == 0 && LinkNear(super, 18, 18) && gPlayerEntity.base.z.HALF.HI > -8) {
        BounceLink(super, 12);
        super->timer = 32;
    }
    if (super->timer) {
        super->timer--;
        SetFrame(super, super->timer > 26 ? AF_BUMPER_BOING : AF_BUMPER_0 + ((super->timer >> 1) & 1));
    } else {
        SetFrame(super, AF_BUMPER_0);
    }
}

/* =============================================================================
 * Debirandos, Kodongos, mini Moldorms, the crystal switch and the shared
 * fireballs (sprite 330)
 * ========================================================================== */

/* ---- fireballs -----------------------------------------------------------------
 * Type 0: the small fireball Debirandos (and others) spit at Link, flying
 * straight at where he stood and fizzling on a wall. Type 1: a Kodongo's fire,
 * rolling along the floor until it meets a wall, then burning there a while.
 * Projectiles: the sword can't hurt them (hit type 72), and they never hold a
 * room's doors shut.
 */
enum { FB_INIT, FB_FLY, FB_BURN };

void AlttpFireball(AlttpEnemy* this) {
    switch (super->action) {
        case FB_INIT:
            AlttpInit(this, super->type == 1 ? AP_KFIRE : AP_FIREBALL,
                      super->type == 1 ? AF_KFIRE_0 : AF_FIREBALL_FLARE);
            super->action = FB_FLY;
            super->timer = 0;
            super->spriteSettings.shadow = 0;
            if (super->speed == 0) {
                super->speed = super->type == 1 ? 0x180 : 0x200;
            }
            break;
        case FB_FLY:
            super->timer++;
            ProcessMovement0(super);
            if (super->collisions != COL_NONE && super->timer > 2) {
                if (super->type == 1) {
                    // the flame catches where it lands
                    super->action = FB_BURN;
                    super->timer = 127;
                    EnqueueSFX(SFX_ITEM_LANTERN_ON);
                } else {
                    CreateFx(super, FX_DEATH, 0);
                    DeleteThisEntity();
                }
            } else if (super->timer > 240) {
                DeleteThisEntity();
            }
            if (super->type == 1) {
                SetFrame(super, AF_KFIRE_0 + ((super->timer >> 2) & 3));
            } else {
                SetFrame(super, super->timer < 4 ? AF_FIREBALL_FLARE : AF_FIREBALL_0 + ((super->timer >> 2) & 1));
            }
            break;
        case FB_BURN: {
            // kFlame_Gfx: the fire rises, flickers, and dies down
            static const u8 burn[16] = { 5, 4, 3, 1, 2, 0, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0 };
            if (--super->timer == 0) {
                DeleteThisEntity();
            }
            SetFrame(super, AF_KFLAME_0 + burn[super->timer >> 3]);
            break;
        }
    }
    super->health = 255;
}

/* ---- Debirando ----------------------------------------------------------------
 * A sand pit in the Desert Palace floor that opens into a whirlpool, dragging
 * Link toward its middle, where a Debirando pops up out of the sand. Type 1 (A
 * Link to the Past's sprite 0x64) is the red kind that spits fireballs at Link
 * while it is up. The pit itself is scenery: when its Debirando is beaten, it
 * closes for good.
 */
enum { DP_INIT, DP_CLOSED, DP_OPENING, DP_OPEN, DP_CLOSING, DP_GONE };

void AlttpDebirandoPit(AlttpEnemy* this) {
    static const u8 opening[4] = { 5, 4, 3, 3 };
    static const u8 closing[4] = { 3, 3, 4, 5 };
    u32 g = 6;
    if (super->action == DP_INIT) {
        Entity* monster;
        EnemyInit((Enemy*)this);
        AlttpInit(this, AP_DEBPIT, AF_DEBPIT_0);
        COLLISION_OFF(super);
        super->health = 0;
        super->spriteSettings.shadow = 0;
        super->spritePriority.b1 = 3;
        super->action = DP_CLOSED;
        super->timer = 1;
        monster = CreateEnemy(ALTTP_DEBIRANDO, super->type);
        if (monster != NULL) {
            CopyPosition(super, monster);
            monster->parent = super;
            super->child = monster;
        }
    }
    if (super->action != DP_GONE && (super->child == NULL || super->child->next == NULL ||
                                      super->child->parent != super ||
                                      (super->child->action != 0 && super->child->health == 0))) {
        // its Debirando is gone: the pit closes
        super->action = DP_GONE;
        super->timer = 63;
    }
    switch (super->action) {
        case DP_CLOSED:
            if (--super->timer == 0) {
                super->action = DP_OPENING;
                super->timer = 63;
            }
            break;
        case DP_OPENING:
            g = opening[super->timer >> 4];
            if (--super->timer == 0) {
                super->action = DP_OPEN;
                super->timer = 255;
            }
            break;
        case DP_OPEN:
            // the sand swirls: three poses in turn
            g = (gRoomTransition.frameCount >> 4) % 3;
            if (--super->timer == 0) {
                super->action = DP_CLOSING;
                super->timer = 63;
            }
            break;
        case DP_CLOSING:
            g = closing[super->timer >> 4];
            if (--super->timer == 0) {
                super->action = DP_CLOSED;
                super->timer = 32;
            }
            break;
        case DP_GONE:
            g = super->timer > 48 ? 3 : super->timer > 24 ? 4 : 5;
            if (--super->timer == 0) {
                DeleteThisEntity();
            }
            break;
    }
    if (g < 3 && LinkNear(super, 18, 18) && gPlayerEntity.base.z.HALF.HI > -4) {
        // the whirlpool drags Link in, a pixel every other frame
        Entity* link = &gPlayerEntity.base;
        if (gRoomTransition.frameCount & 1) {
            s32 dx = super->x.HALF.HI - link->x.HALF.HI;
            s32 dy = super->y.HALF.HI - link->y.HALF.HI;
            link->x.HALF.HI += dx > 0 ? 1 : dx < 0 ? -1 : 0;
            link->y.HALF.HI += dy > 0 ? 1 : dy < 0 ? -1 : 0;
        }
    }
    if (g == 6) {
        super->spriteSettings.draw = 0;
    } else {
        super->spriteSettings.draw = 1;
        SetFrame(super, AF_DEBPIT_0 + g);
    }
}

enum { DB_INIT, DB_UNDER, DB_EMERGE, DB_UP, DB_SUBMERGE };

static void DebirandoSetVisible(AlttpEnemy* this, bool32 on) {
    super->spriteSettings.draw = on;
    if (on) {
        COLLISION_ON(super);
    } else {
        COLLISION_OFF(super);
    }
}

static void DebirandoTick(AlttpEnemy* this) {
    u32 f = AF_DEBIRANDO_0;
    switch (super->action) {
        case DB_INIT:
            AlttpInit(this, super->type ? AP_DEBIRANDO_FIRE : AP_DEBIRANDO, AF_DEBIRANDO_0);
            super->action = DB_UNDER;
            super->timer = 96;
            super->spriteSettings.shadow = 0;
            DebirandoSetVisible(this, FALSE);
            break;
        case DB_UNDER:
            if (--super->timer == 0) {
                super->action = DB_EMERGE;
                super->timer = 31;
                DebirandoSetVisible(this, TRUE);
                EnqueueSFX(SFX_PLACE_OBJ);
            }
            return;
        case DB_EMERGE:
            f = AF_DEBIRANDO_0 + (super->timer >> 4 ? 0 : 1);
            if (--super->timer == 0) {
                super->action = DB_UP;
                super->timer = 128;
            }
            break;
        case DB_UP:
            // jaws snapping, and the red kind spits fire every half second
            this->step++;
            f = AF_DEBIRANDO_2 + ((this->step >> 3) & 1);
            if (this->pose) {
                this->pose--;
                f = AF_DEBIRANDO_SNAP;
            }
            if (super->type && (super->timer & 31) == 0) {
                Entity* fb = CreateEnemy(ALTTP_FIREBALL, 0);
                if (fb != NULL) {
                    CopyPosition(super, fb);
                    fb->y.HALF.HI -= 6;
                    fb->direction = GetFacingDirection(super, &gPlayerEntity.base);
                    fb->parent = super;
                    this->pose = 6;
                    EnqueueSFX(SFX_ITEM_LANTERN_ON);
                }
            }
            if (--super->timer == 0) {
                super->action = DB_SUBMERGE;
                super->timer = 31;
            }
            break;
        case DB_SUBMERGE:
            f = super->timer > 24 ? AF_DEBIRANDO_GULP : AF_DEBIRANDO_0 + (super->timer >> 4 ? 1 : 0);
            if (--super->timer == 0) {
                super->action = DB_UNDER;
                super->timer = 223;
                DebirandoSetVisible(this, FALSE);
            }
            break;
    }
    SetFrame(super, f);
    AlttpEndTick(this);
}

static void DebirandoOnCollision(AlttpEnemy* this);
static void DebirandoOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, LEEVER);
}

static void (*const Debirando_Functions[])(AlttpEnemy*) = {
    DebirandoTick, DebirandoOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    DebirandoOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void DebirandoOnCollision(AlttpEnemy* this) {
    super->knockbackDuration = 0; // it is rooted in the sand
    AlttpOnCollision(this, (void (*const*)())Debirando_Functions);
}

void AlttpDebirando(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Debirando_Functions);
}

/* ---- Kodongo ------------------------------------------------------------------
 * A fire-breathing lizard of the Tower of Hera and the Palace of Darkness. It
 * walks the floor's grid in straight lines, turning back at walls; now and then,
 * at a crossing, it stops, puffs up and breathes a string of fireballs ahead of
 * it, which burn on where they hit a wall. Fire can't hurt it.
 */
enum { KD_INIT, KD_PICK, KD_WALK, KD_BREATHE };

static void KodongoSetDir(AlttpEnemy* this) {
    super->direction = sFaceToDir[this->face];
    super->speed = 0x80;
}

static void KodongoTick(AlttpEnemy* this) {
    static const u8 walkGfx[8] = { 2, 2, 0, 5, 3, 3, 0, 5 };
    static const u8 walkFlip[8] = { 1, 0, 0, 0, 1, 0, 1, 1 };
    static const u8 flameGfx[8] = { 2, 2, 0, 5, 4, 4, 1, 6 };
    u32 j, f;
    switch (super->action) {
        case KD_INIT:
            AlttpInit(this, AP_KODONGO, AF_KODONGO_0);
            super->action = KD_PICK;
            break;
        case KD_PICK: {
            u32 tries;
            this->face = Random() & 3;
            for (tries = 0; tries < 4; tries++) {
                static const s8 dx[4] = { 12, -12, 0, 0 };
                static const s8 dy[4] = { 0, 0, 12, -12 };
                if (GetCollisionDataAtWorldCoords(super->x.HALF.HI + dx[this->face], super->y.HALF.HI + dy[this->face],
                                                  super->collisionLayer) == 0) {
                    break;
                }
                this->face = (this->face + 1) & 3;
            }
            KodongoSetDir(this);
            super->action = KD_WALK;
            this->turns = 16;
            break;
        }
        case KD_WALK: {
            u32 crossing;
            ProcessMovement0(super);
            this->step++;
            if (super->collisions != COL_NONE) {
                this->face ^= 1;
                KodongoSetDir(this);
            }
            // on a crossing of the floor grid, now and then, it stops to breathe fire
            crossing = ((super->x.HALF.HI - gRoomControls.origin_x) & 15) == 8 &&
                       ((super->y.HALF.HI - gRoomControls.origin_y) & 15) == 8;
            if (this->turns) {
                this->turns--;
            } else if (crossing && (Random() & 3) == 0) {
                super->action = KD_BREATHE;
                super->timer = 111;
                this->turns = 32;
            }
            break;
        }
        case KD_BREATHE:
            if (--super->timer == 0) {
                super->action = KD_PICK;
            } else if (super->timer >= 0x20 && super->timer < 0x50 && (super->timer & 15) == 0) {
                static const s8 fx[4] = { 8, -8, 0, 0 };
                static const s8 fy[4] = { 0, 0, 8, -8 };
                Entity* fire = CreateEnemy(ALTTP_FIREBALL, 1);
                if (fire != NULL) {
                    CopyPosition(super, fire);
                    fire->x.HALF.HI += fx[this->face];
                    fire->y.HALF.HI += fy[this->face] - 2;
                    fire->direction = sFaceToDir[this->face];
                    fire->speed = 0x180;
                    fire->parent = super;
                    EnqueueSFX(SFX_ITEM_LANTERN_ON);
                }
            }
            break;
    }
    j = ((this->step >> 2) & 4) | this->face;
    if (super->action == KD_BREATHE) {
        bool32 breathing = super->timer >= 0x20 && super->timer < 0x50;
        if (!breathing && super->timer >= 0x50) {
            // an added wind-up: the chest swells before the fire
            static const u8 puff[4] = { AF_KODONGO_PUFF_4, AF_KODONGO_PUFF_4, AF_KODONGO_PUFF_1, AF_KODONGO_PUFF_6 };
            f = super->timer < 0x60 ? puff[this->face] : AF_KODONGO_0 + walkGfx[this->face] * 2 + walkFlip[this->face];
        } else {
            f = AF_KODONGO_0 + flameGfx[breathing * 4 + this->face] * 2 + walkFlip[this->face];
        }
    } else {
        f = AF_KODONGO_0 + walkGfx[j] * 2 + walkFlip[j];
    }
    SetFrame(super, f);
    AlttpEndTick(this);
}

static void KodongoOnCollision(AlttpEnemy* this);
static void KodongoOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, ROLLOBITE);
}

static void (*const Kodongo_Functions[])(AlttpEnemy*) = {
    KodongoTick, KodongoOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    KodongoOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void KodongoOnCollision(AlttpEnemy* this) {
    AlttpOnCollision(this, (void (*const*)())Kodongo_Functions);
}

void AlttpKodongo(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Kodongo_Functions);
    EnemySetFXOffset(super, 0, 1, -12);
}

/* ---- mini Moldorm ---------------------------------------------------------------
 * A little worm that races about, weaving at random and every sixth turn
 * swerving round to chase Link, its eyes looking where it goes. It bounces off
 * walls. Its segment and tail trail behind it (type 1 and 2 of the segment).
 */
enum { MM_INIT, MM_STRAIGHT, MM_MEANDER, MM_SEEK };

static void MiniMoldormTick(AlttpEnemy* this) {
    u32 i;
    switch (super->action) {
        case MM_INIT:
            AlttpInit(this, AP_MINIMOLD, AF_MINIMOLD_HEAD_0);
            this->face = Random() & 15;
            super->action = MM_STRAIGHT;
            super->timer = 16;
            {
                Entity* lead = super;
                for (i = 1; i <= 2; i++) {
                    Entity* seg = CreateEnemy(MINI_MOLDORM_SEGMENT, i);
                    if (seg != NULL) {
                        seg->parent = super;
                        seg->child = lead; // the one it follows
                        CopyPosition(super, seg);
                        lead = seg;
                    }
                }
            }
            break;
        case MM_STRAIGHT:
            if (--super->timer == 0) {
                if (++this->turns == 6) {
                    this->turns = 0;
                    super->action = MM_SEEK;
                } else {
                    super->action = MM_MEANDER;
                }
                this->head = (Random() & 2) ? 1 : 15;
                super->timer = (Random() & 31) + 32;
            }
            break;
        case MM_MEANDER:
            if (--super->timer == 0) {
                super->timer = (Random() & 15) + 8;
                super->action = MM_STRAIGHT;
            } else if ((super->timer & 3) == 0) {
                this->face = (this->face + this->head) & 15;
            }
            break;
        case MM_SEEK:
            if ((++this->cooldown & 3) == 0) {
                u32 want = AngleToHeading(AngleTo(gPlayerEntity.base.x.HALF.HI - super->x.HALF.HI,
                                                  gPlayerEntity.base.y.HALF.HI - super->y.HALF.HI));
                s32 diff = (want - this->face) & 15;
                if (diff == 0) {
                    super->action = MM_STRAIGHT;
                    super->timer = 48;
                } else {
                    this->face = (this->face + (diff < 8 ? 1 : 15)) & 15;
                }
            }
            break;
    }
    super->direction = (this->face * 2 + 8) & 0x1f;
    super->speed = 0x180;
    ProcessMovement0(super);
    if (super->collisions != COL_NONE) {
        // bounce straight back, and maybe weave the other way
        this->face = (this->face + 8) & 15;
        if (Random() & 1) {
            this->head = 16 - this->head;
        }
        this->pose = 6;
    }
    this->step++;
    if (this->pose) {
        this->pose--;
        SetFrame(super, AF_MINIMOLD_SQUASH);
    } else {
        SetFrame(super, AF_MINIMOLD_HEAD_0 + this->face);
    }
    AlttpEndTick(this);
}

static void MiniMoldormOnCollision(AlttpEnemy* this);
static void MiniMoldormOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, ROPE);
}

static void (*const MiniMoldorm_Functions[])(AlttpEnemy*) = {
    MiniMoldormTick, MiniMoldormOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    MiniMoldormOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void MiniMoldormOnCollision(AlttpEnemy* this) {
    AlttpOnCollision(this, (void (*const*)())MiniMoldorm_Functions);
}

void MiniMoldorm(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)MiniMoldorm_Functions);
}

/** Type 1 the segment, 2 the tail: each keeps a few pixels behind the one ahead. */
void MiniMoldormSegment(AlttpEnemy* this) {
    Entity* lead;
    s32 dx, dy, d, gap;
    if (!ParentAlive(super)) {
        DeleteThisEntity();
    }
    if (super->action == 0) {
        super->flags |= ENT_DID_INIT;
        AlttpInit(this, AP_MINIMOLD, super->type == 1 ? AF_MINIMOLD_BODY : AF_MINIMOLD_TAIL);
        COLLISION_OFF(super);
        super->health = 0;
        super->spritePriority.b1 = 2;
        super->spriteSettings.shadow = 0;
    }
    lead = super->child;
    if (lead == NULL || lead->next == NULL || lead->kind != ENEMY) {
        lead = super->parent;
    }
    gap = super->type == 1 ? 7 : 6;
    dx = super->x.HALF.HI - lead->x.HALF.HI;
    dy = super->y.HALF.HI - lead->y.HALF.HI;
    d = Distance(dx, dy);
    if (d > gap) {
        super->x.HALF.HI = lead->x.HALF.HI + Div(dx * gap, d);
        super->y.HALF.HI = lead->y.HALF.HI + Div(dy * gap, d);
    }
    super->collisionLayer = super->parent->collisionLayer;
    SetFrame(super, super->type == 1 ? AF_MINIMOLD_BODY : AF_MINIMOLD_TAIL);
}

/* =============================================================================
 * The Dark World dungeons: the Palace of Darkness, the Swamp Palace and Skull
 * Woods. Their enemies (sprite 331) and bosses (sprite 332).
 * ========================================================================== */
#include "blend.h"
#include "playeritem.h"

#define HOOK_NONE 0

/** Moves by an A Link to the Past velocity (1/16 pixel a frame). */
static void MoveVel(Entity* e, s32 vx, s32 vy) {
    e->x.WORD += vx << 12;
    e->y.WORD += vy << 12;
}

static bool32 WallAt(Entity* e, s32 dx, s32 dy) {
    return GetCollisionDataAtWorldCoords(e->x.HALF.HI + dx, e->y.HALF.HI + dy, e->collisionLayer) != 0;
}

/** A wall a shot breaks on: not the pit edges (0x20 and up) that only keep walkers out. */
static bool32 ShotWallAt(Entity* e) {
    u32 c = GetCollisionDataAtWorldCoords(e->x.HALF.HI, e->y.HALF.HI, e->collisionLayer);
    return c != 0 && (c < 0x20 || c == 0xff);
}

/** Moves by a velocity, stopping at walls (reach: how far the body sticks out).
 * Returns 1 when blocked sideways, 2 when blocked up or down. */
static u32 MoveVelWalls(Entity* e, s32 vx, s32 vy, s32 reach) {
    u32 hit = 0;
    if (vx != 0) {
        if (WallAt(e, vx > 0 ? reach : -reach, 0)) {
            hit |= 1;
        } else {
            e->x.WORD += vx << 12;
        }
    }
    if (vy != 0) {
        if (WallAt(e, 0, vy > 0 ? reach : -reach)) {
            hit |= 2;
        } else {
            e->y.WORD += vy << 12;
        }
    }
    return hit;
}

/** A velocity of the given speed toward a point (Sprite_ProjectSpeedTowardsLocation). */
static void AimVel(Entity* e, s32 tx, s32 ty, s32 speed, s16* vx, s16* vy) {
    u32 a = AngleTo(tx - e->x.HALF.HI, ty - e->y.HALF.HI);
    *vx = (Sin256(a) * speed) >> 8;
    *vy = -(Sin256(a + 64) * speed) >> 8;
}

/** sin of an angle where 0x200 is a full turn, times r (HelmasaurSin, ArrgiSin). */
static s32 HalfSin(u32 a, s32 r) {
    return (Sin256((a & 0x1ff) >> 1) * r) >> 8;
}

static void ApproachVel(s16* v, s32 target) {
    if (*v < target) {
        (*v)++;
    } else if (*v > target) {
        (*v)--;
    }
}

static bool32 IsHook(Entity* e) {
    return e != NULL && e->kind == OBJECT && e->id == LINK_PARTNER && e->type == 3;
}

/** A blow from the Hammer (or a medallion's strike) or a bomb blast. */
static bool32 IsHeavy(Entity* e) {
    return e != NULL && ((e->kind == OBJECT && e->id == LINK_PARTNER && e->type == 2) ||
                         (e->kind == PLAYER_ITEM && e->id == PLAYER_ITEM_BOMB));
}

/** A bomb's blast: the smoke it leaves (SMOKE_PARTICLE, a child of the bomb). */
static bool32 IsBlast(Entity* e) {
    return e != NULL && e->kind == OBJECT && e->id == SMOKE_PARTICLE;
}

/** The Hammer coming down within range of a point. */
static bool32 HammerNear(Entity* at, s32 range) {
    LinkedList* list = &gEntityLists[6];
    Entity* e;
    u32 i;
    for (i = 0; i < ARRAY_COUNT(gEntityLists); i++) {
        list = &gEntityLists[i];
        for (e = list->first; (u32)e != (u32)list; e = e->next) {
            if (e->kind == OBJECT && e->id == LINK_PARTNER && e->type == 2 &&
                Abs(e->x.HALF.HI - at->x.HALF.HI) < range && Abs(e->y.HALF.HI - at->y.HALF.HI) < range) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

/**
 * A boss that A Link to the Past starts against its room's top wall starts lower:
 * the Game Boy Advance's screen is shorter, so with Link at the door it would be under
 * the hearts. Moves it down by up to n pixels, keeping clear of walls.
 */
static void BossStartDown(Entity* e, s32 n) {
    while (n > 0 && !WallAt(e, 0, 24)) {
        e->y.HALF.HI += 4;
        n -= 4;
    }
}

static bool32 InRoom(Entity* e, s32 margin) {
    s32 x = e->x.HALF.HI - gRoomControls.origin_x;
    s32 y = e->y.HALF.HI - gRoomControls.origin_y;
    return x > -margin && y > -margin && x < gRoomControls.width + margin && y < gRoomControls.height + margin;
}

/** A Link to the Past height (pixels up) as Minish Cap's z. */
static void SetHeight(Entity* e, s32 z) {
    e->z.WORD = -z << 16;
}

static Entity* SpawnAt(u32 id, u32 type, Entity* from, s32 dx, s32 dy) {
    Entity* e = CreateEnemy(id, type);
    if (e != NULL) {
        CopyPosition(from, e);
        e->x.HALF.HI += dx;
        e->y.HALF.HI += dy;
        e->parent = from;
    }
    return e;
}

/** Fades a beaten boss out in a string of blasts, then one big one. */
static bool32 BossBurst(AlttpEnemy* this, s32 dx, s32 dy) {
    if (this->burst == 0) {
        COLLISION_OFF(super);
        EnqueueSFX(SFX_BOSS_DIE);
    }
    if (this->burst < 96) {
        this->burst++;
        if ((this->burst & 7) == 0) {
            Entity* fx = CreateFx(super, FX_GIANT_EXPLOSION4, 0);
            if (fx != NULL) {
                fx->x.HALF.HI += dx * ((this->burst >> 3) & 3) - dx * 2 + ((Random() & 15) - 8);
                fx->y.HALF.HI += dy * ((this->burst >> 4) & 3) - dy + ((Random() & 15) - 8);
            }
            EnqueueSFX(SFX_ITEM_BOMB_EXPLODE);
        }
        super->spriteSettings.draw = (this->burst >> 1) & 1 ? 1 : this->burst < 64;
        return FALSE;
    }
    CreateFx(super, FX_GIANT_EXPLOSION4, 0);
    EnqueueSFX(SFX_BOSS_EXPLODE);
    return TRUE;
}

/* ---- spike block ------------------------------------------------------------------
 * Type 0: a blade trap sliding back and forth across a room, turning at walls.
 * Type 1: one of Mothula's: it bursts out of the wall (parent: Mothula), rumbles,
 * races across the room, and slides back into its wall. Spike blocks can't be hurt
 * and never hold doors shut.
 */
enum { SB_INIT, SB_SLIDE, SB_RUMBLE, SB_CHARGE, SB_WAIT, SB_RETURN };

void AlttpSpikeBlock(AlttpEnemy* this) {
    static const s8 dirX[4] = { 1, -1, 0, 0 };
    static const s8 dirY[4] = { 0, 0, 1, -1 };
    u32 d = this->face;
    switch (super->action) {
        case SB_INIT:
            AlttpInit(this, AP_SPIKEBLOCK, AF_SPIKEBLOCK);
            super->health = 0;
            super->spriteSettings.shadow = 0;
            this->homeX = super->x.HALF.HI;
            this->homeY = super->y.HALF.HI;
            if (super->type == 0) {
                super->action = SB_SLIDE;
                this->targetX = 32;
            } else {
                super->action = SB_RUMBLE;
                super->timer = 64;
                this->targetX = this->targetY = 0;
                EnqueueSFX(SFX_PLACE_OBJ);
            }
            break;
        case SB_SLIDE:
            if (super->timer) {
                super->timer--;
            }
            if (MoveVelWalls(super, this->targetX, 0, 8) && super->timer == 0) {
                this->targetX = -this->targetX;
                super->timer = 4;
                EnqueueSFX(SFX_METAL_CLINK);
            }
            break;
        case SB_RUMBLE:
            if (--super->timer == 0) {
                super->x.HALF.HI = this->homeX;
                super->y.HALF.HI = this->homeY;
                super->action = SB_CHARGE;
                super->timer = 40; // clear of its own wall first
            } else {
                super->x.HALF.HI = this->homeX + ((super->timer >> 1) & 1 ? -1 : 1);
            }
            break;
        case SB_CHARGE:
            if (this->targetX != dirX[d] * 32) {
                this->targetX += dirX[d];
            }
            if (this->targetY != dirY[d] * 32) {
                this->targetY += dirY[d];
            }
            if (super->timer) {
                super->timer--;
                MoveVel(super, this->targetX, this->targetY);
            } else if (MoveVelWalls(super, this->targetX, this->targetY, 8) || !InRoom(super, -16) ||
                       ++this->step > 200) {
                super->action = SB_WAIT;
                super->timer = 64;
                EnqueueSFX(SFX_METAL_CLINK);
            }
            break;
        case SB_WAIT:
            if (--super->timer == 0) {
                super->action = SB_RETURN;
            }
            break;
        case SB_RETURN:
            MoveVel(super, -dirX[d] * 16, -dirY[d] * 16);
            if (Abs(super->x.HALF.HI - this->homeX) + Abs(super->y.HALF.HI - this->homeY) < 2) {
                DeleteThisEntity();
            }
            break;
    }
    if (super->type == 1 && !ParentAlive(super)) {
        CreateFx(super, FX_DEATH, 0);
        DeleteThisEntity();
    }
    super->health = 0;
    super->knockbackDuration = 0;
}

/* ---- mini Helmasaur ------------------------------------------------------------
 * Skids after Link with a beetle's momentum, re-aiming twice a second. Its mask
 * turns the sword from the front (a clink that knocks it back); from the side or
 * behind it can be beaten.
 */
static void MiniHelmasaurTick(AlttpEnemy* this) {
    static const u8 gfx[8] = { 3, 4, 3, 4, 2, 2, 5, 5 };
    static const u8 flip[8] = { 1, 1, 0, 0, 0, 1, 1, 0 };
    s16* vx = &this->targetX;
    s16* vy = &this->targetY;
    u32 j;
    if (super->action == 0) {
        AlttpInit(this, AP_MINIHELM, AF_MINIHELM_2);
        *vx = *vy = 0;
    }
    if ((this->step++ & 31) == 0) {
        AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI, 0x100, &this->homeX, &this->homeY);
    }
    if ((this->step & 1) == 0) {
        *vx += *vx < this->homeX ? 16 : -16;
        *vy += *vy < this->homeY ? 16 : -16;
    }
    super->direction = (AngleTo(*vx, *vy) + 4) >> 3 & 0x1f;
    super->speed = Distance(*vx, *vy);
    if (*vx || *vy) {
        ProcessMovement0(super);
    }
    if (super->collisions & (COL_EAST_ANY | COL_WEST_ANY)) {
        *vx = 0;
    }
    if (super->collisions & (COL_NORTH_ANY | COL_SOUTH_ANY)) {
        *vy = 0;
    }
    if ((this->step & 15) == 0) {
        // which way it faces: 0 right, 1 left, 2 down, 3 up
        this->face = Abs(*vx) >= Abs(*vy) ? (*vx < 0) : 2 + (*vy < 0);
    }
    if (this->pose) {
        this->pose--;
        SetFrame(super, AF_MINIHELM_SQUASH);
    } else {
        j = ((this->step >> 2) & 1) | this->face << 1;
        SetFrame(super, AF_MINIHELM_2 + (gfx[j] - 2) * 2 + flip[j]);
    }
    AlttpEndTick(this);
}

static void MiniHelmasaurOnCollision(AlttpEnemy* this);
static void MiniHelmasaurOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, BEETLE);
}

static void (*const MiniHelmasaur_Functions[])(AlttpEnemy*) = {
    MiniHelmasaurTick, MiniHelmasaurOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    MiniHelmasaurOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void MiniHelmasaurOnCollision(AlttpEnemy* this) {
    if (super->health < this->savedHealth && !IsHeavy(super->contactedEntity) &&
        sFaceToDir[this->face] == ((GetFacingDirection(super, &gPlayerEntity.base) + 4) & 0x18)) {
        // the sword glances off the mask, and the blow sends it skidding back
        RestoreHealth(this);
        super->knockbackDuration = 12;
        super->knockbackSpeed = 0x280;
        this->targetX = this->targetY = 0;
        this->pose = 8;
        EnqueueSFX(SFX_METAL_CLINK);
    } else if (super->health < this->savedHealth) {
        EnqueueSFX(SFX_HIT);
    }
    EnemyFunctionHandlerAfterCollision(super, (void (*const*)())MiniHelmasaur_Functions);
}

void MiniHelmasaur(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)MiniHelmasaur_Functions);
}

/* ---- Terrorpin -------------------------------------------------------------------
 * A turtle in a spiked shell, plodding toward Link; the sword only clinks off it.
 * A blow of the Hammer close by flips it over, and then it can be beaten, until it
 * rocks itself back onto its feet, quicker than before.
 */
static void TerrorpinFlip(AlttpEnemy* this) {
    this->targetX = -this->targetX;
    this->targetY = -this->targetY;
    this->cooldown = 32;
    this->turns = 4;    // faster from now on
    this->coerced ^= 1; // overturned
    this->homeY = 32;   // hop
    super->timer = this->coerced ? 255 : 64;
    this->shield = !this->coerced;
    EnqueueSFX(SFX_14C);
}

static void TerrorpinTick(AlttpEnemy* this) {
    static const s8 velX[8] = { 8, -8, 0, 0, 12, -12, 0, 0 };
    static const s8 velY[8] = { 0, 0, 8, -8, 0, 0, 12, -12 };
    s32 z;
    if (super->action == 0) {
        AlttpInit(this, AP_TERRORPIN, AF_TERRORPIN_0);
        this->shield = 1;
        this->homeX = 0;
        this->homeY = 0;
    }
    this->step++;
    if (this->cooldown) {
        this->cooldown--;
    } else if (this->homeX == 0 && !this->coerced && HammerNear(super, 24)) {
        TerrorpinFlip(this);
    }
    // height (homeX, 1/16 pixels) and its speed up (homeY)
    this->homeX += this->homeY;
    this->homeY -= 2;
    if (this->homeX <= 0) {
        this->homeX = 0;
        if (this->coerced) {
            // overturned: it bounces to a stop
            z = -this->homeY >> 1;
            this->homeY = z < 9 ? 0 : z;
            this->targetX >>= 1;
            this->targetY >>= 1;
        } else {
            this->homeY = 0;
        }
    }
    SetHeight(super, this->homeX >> 4);
    if (!this->coerced) {
        if (super->timer == 0) {
            super->timer = (Random() & 31) + 32;
            this->face = FaceTowardLink(super);
        } else {
            super->timer--;
        }
        this->targetX = velX[this->face + this->turns];
        this->targetY = velY[this->face + this->turns];
        SetFrame(super, AF_TERRORPIN_0 + ((this->step >> (this->turns ? 2 : 3)) & 1));
    } else {
        if (--super->timer == 0) {
            // back on its feet
            this->coerced = 0;
            this->shield = 1;
            this->homeY = 32;
            super->timer = 64;
            EnqueueSFX(SFX_14C);
        } else if (super->timer < 64) {
            this->targetX = (super->timer >> 1) & 1 ? -8 : 8;
        }
        SetFrame(super, this->homeX > 0 ? AF_TERRORPIN_HOP : AF_TERRORPIN_2 + ((this->step >> 3) & 1));
    }
    MoveVelWalls(super, this->targetX, this->targetY, 7);
    AlttpEndTick(this);
}

static void TerrorpinOnCollision(AlttpEnemy* this);
static void TerrorpinOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, SPINY_BEETLE);
}

static void (*const Terrorpin_Functions[])(AlttpEnemy*) = {
    TerrorpinTick, TerrorpinOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    TerrorpinOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void TerrorpinOnCollision(AlttpEnemy* this) {
    if (IsHeavy(super->contactedEntity) && this->cooldown != 0) {
        // the same blow that just flipped it
        RestoreHealth(this);
        super->knockbackDuration = 0;
    } else if (IsHeavy(super->contactedEntity) && !this->coerced) {
        RestoreHealth(this);
        super->knockbackDuration = 0;
        TerrorpinFlip(this);
    } else if (this->shield) {
        BlockHit(this);
    } else if (super->health < this->savedHealth) {
        EnqueueSFX(SFX_HIT);
    }
    EnemyFunctionHandlerAfterCollision(super, (void (*const*)())Terrorpin_Functions);
}

void Terrorpin(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Terrorpin_Functions);
}

/* ---- Hover -----------------------------------------------------------------------
 * The Swamp Palace's water strider. It rests, then darts toward Link's side of it,
 * speeding up and then gliding to a stop.
 */
enum { HV_INIT, HV_REST, HV_DART };

static void HoverTick(AlttpEnemy* this) {
    static const s8 accX[4] = { 1, -1, 1, -1 };
    static const s8 accY[4] = { 1, 1, -1, -1 };
    u32 g;
    u32 d = this->face;
    this->step++;
    switch (super->action) {
        case HV_INIT:
            AlttpInit(this, AP_HOVER, AF_HOVER_0);
            super->action = HV_REST;
            super->timer = 32;
            this->targetX = this->targetY = 0;
            break;
        case HV_REST:
            if (--super->timer == 0) {
                super->action = HV_DART;
                this->face = d = (super->x.HALF.HI > gPlayerEntity.base.x.HALF.HI) +
                                 (super->y.HALF.HI > gPlayerEntity.base.y.HALF.HI) * 2;
                super->timer = (Random() & 15) + 12;
                this->targetX = this->targetY = 0;
            }
            break;
        case HV_DART:
            if (super->timer) {
                super->timer--;
                this->targetX += accX[d];
                this->targetY += accY[d];
            } else {
                this->targetX -= accX[d];
                this->targetY -= accY[d];
                if (this->targetY == 0) {
                    this->targetX = 0;
                    super->action = HV_REST;
                    super->timer = 64;
                }
            }
            break;
    }
    if (MoveVelWalls(super, this->targetX, this->targetY, 7)) {
        this->targetX = this->targetY = 0;
        if (super->action == HV_DART) {
            super->action = HV_REST;
            super->timer = 64;
        }
    }
    g = super->action == HV_DART && super->timer ? (this->step >> 3) & 1 : (this->step >> 3) & 2;
    SetFrame(super, AF_HOVER_0 + g * 2 + !(this->face & 1));
    AlttpEndTick(this);
}

static void HoverOnCollision(AlttpEnemy* this);
static void HoverOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, TEKTITE);
}

static void (*const Hover_Functions[])(AlttpEnemy*) = {
    HoverTick, HoverOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    HoverOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void HoverOnCollision(AlttpEnemy* this) {
    if (super->health < this->savedHealth) {
        super->action = HV_REST;
        super->timer = 32;
        this->targetX = this->targetY = 0;
    }
    AlttpOnCollision(this, (void (*const*)())Hover_Functions);
}

void Hover(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Hover_Functions);
}

/* ---- Kyameron ---------------------------------------------------------------------
 * A drop of the swamp's water: a puddle wells up, gathers into a ball, and bounces
 * off the walls toward Link, glittering, until it bursts on the third wall (or on
 * Link) and the puddle forms again where it started.
 */
enum { KY_INIT, KY_RESET, KY_PUDDLE, KY_FORM, KY_BOUNCE, KY_BURST };

static void KyameronShow(AlttpEnemy* this, bool32 on) {
    super->spriteSettings.draw = on;
}

static void KyameronTick(AlttpEnemy* this) {
    static const u8 form[8] = { 4, 7, 14, 13, 12, 6, 6, 5 };
    static const u8 bounce[4] = { 3, 2, 1, 0 };
    u32 hit;
    switch (super->action) {
        case KY_INIT:
            AlttpInit(this, AP_KYAM, AF_KYAM_8);
            super->spriteSettings.shadow = 0;
            this->homeX = super->x.HALF.HI;
            this->homeY = super->y.HALF.HI;
            super->action = KY_RESET;
            super->timer = 1;
            COLLISION_OFF(super);
            KyameronShow(this, FALSE);
            break;
        case KY_RESET:
            if (--super->timer == 0) {
                super->action = KY_PUDDLE;
                super->timer = (Random() & 63) + 96;
                super->x.HALF.HI = this->homeX;
                super->y.HALF.HI = this->homeY;
                this->step = 5;
                this->head = 8;
                KyameronShow(this, TRUE);
            }
            break;
        case KY_PUDDLE:
            if (--super->timer == 0) {
                super->action = KY_FORM;
                super->timer = 31;
            }
            if (this->step-- == 0) {
                this->step = 5;
                this->head = ((this->head + 1) & 3) + 8;
            }
            SetFrame(super, AF_KYAM_0 + this->head);
            break;
        case KY_FORM:
            if (--super->timer == 0) {
                u32 j = (super->y.HALF.HI > gPlayerEntity.base.y.HALF.HI) * 2 +
                        (super->x.HALF.HI > gPlayerEntity.base.x.HALF.HI);
                super->action = KY_BOUNCE;
                this->targetX = j & 1 ? -32 : 32;
                this->targetY = j & 2 ? -32 : 32;
                this->turns = 0;
                COLLISION_ON(super);
            } else {
                if (super->timer == 7) {
                    super->y.HALF.HI -= 29;
                }
                SetFrame(super, AF_KYAM_0 + form[super->timer >> 2]);
            }
            break;
        case KY_BOUNCE:
            MoveVel(super, this->targetX, this->targetY);
            hit = 0;
            if (WallAt(super, this->targetX > 0 ? 7 : -7, 0)) {
                this->targetX = -this->targetX;
                hit = 1;
            }
            if (WallAt(super, 0, this->targetY > 0 ? 7 : -7)) {
                this->targetY = -this->targetY;
                hit = 1;
            }
            this->turns += hit;
            if (this->turns >= 3 || (super->contactFlags & CONTACT_NOW)) {
                super->action = KY_BURST;
                super->timer = 15;
                COLLISION_OFF(super);
                EnqueueSFX(SFX_WATER_SPLASH);
            }
            SetFrame(super, AF_KYAM_0 + bounce[(++this->step >> 3) & 3]);
            if ((super->timer++ & 7) == 0) {
                Entity* fx = CreateFx(super, FX_SPARKLE, 0);
                if (fx != NULL) {
                    fx->x.HALF.HI += (Random() & 15) - 8;
                    fx->y.HALF.HI += (Random() & 15) - 8;
                }
            }
            break;
        case KY_BURST:
            if (--super->timer == 0) {
                super->action = KY_RESET;
                super->timer = 64;
                KyameronShow(this, FALSE);
            } else {
                SetFrame(super, AF_KYAM_0 + 15 + (super->timer >> 2));
            }
            break;
    }
    AlttpEndTick(this);
}

static void KyameronOnCollision(AlttpEnemy* this);
static void KyameronOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, CHUCHU);
}

static void (*const Kyameron_Functions[])(AlttpEnemy*) = {
    KyameronTick, KyameronOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    KyameronOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void KyameronOnCollision(AlttpEnemy* this) {
    super->knockbackDuration = 0;
    AlttpOnCollision(this, (void (*const*)())Kyameron_Functions);
}

void Kyameron(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Kyameron_Functions);
}

/* ---- Zol ----------------------------------------------------------------------------
 * A blob hiding unseen in the floor. When Link walks over it, it wells up right
 * under him, leaps at him, wobbles where it lands, and then hops after him in
 * little bursts.
 */
enum { ZL_INIT, ZL_HIDE, ZL_RISE, ZL_LEAP, ZL_WOBBLE, ZL_HOP };

static void ZolTick(AlttpEnemy* this) {
    static const u8 rise[16] = { 0, 1, 7, 7, 6, 6, 5, 5, 6, 6, 5, 5, 4, 4, 4, 4 };
    u32 g = 0;
    s32 z;
    switch (super->action) {
        case ZL_INIT:
            AlttpInit(this, AP_ZOL, AF_ZOL_4);
            super->action = ZL_HIDE;
            super->spriteSettings.draw = 0;
            super->spriteSettings.shadow = 0;
            COLLISION_OFF(super);
            return;
        case ZL_HIDE:
            if (LinkNear(super, 12, 12)) {
                super->action = ZL_RISE;
                super->timer = 127;
                super->x.HALF.HI = gPlayerEntity.base.x.HALF.HI;
                super->y.HALF.HI = gPlayerEntity.base.y.HALF.HI + 8;
                super->spriteSettings.draw = 1;
            }
            return;
        case ZL_RISE: {
            u32 r = rise[super->timer >> 3];
            if (--super->timer == 0) {
                super->action = ZL_LEAP;
                this->homeX = 0;
                this->homeY = 32;
                AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI, 16, &this->targetX,
                       &this->targetY);
                COLLISION_ON(super);
                super->spriteSettings.shadow = 1;
                EnqueueSFX(SFX_PLACE_OBJ);
            }
            SetFrame(super, r < 4 ? AF_ZOL_0 + r * 2 : AF_ZOL_4 + r - 4);
            AlttpEndTick(this);
            return;
        }
        case ZL_LEAP:
            MoveVelWalls(super, this->targetX, this->targetY, 6);
            this->homeX += this->homeY;
            if (this->homeY > -64) {
                this->homeY -= 2;
            }
            if (this->homeX <= 0) {
                this->homeX = 0;
                super->action = ZL_WOBBLE;
                super->timer = 31;
            }
            g = 0;
            break;
        case ZL_WOBBLE:
            if (--super->timer == 0) {
                super->action = ZL_HOP;
                super->timer = 1;
                this->turns = 0;
                this->head = 8;
            }
            super->x.HALF.HI += (super->timer >> 1) & 1 ? -1 : 1;
            g = super->timer > 16;
            if (super->timer > 24) {
                SetFrame(super, AF_ZOL_LAND);
                AlttpEndTick(this);
                return;
            }
            break;
        case ZL_HOP:
            if (--super->timer == 0) {
                AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI, 48, &this->targetX,
                       &this->targetY);
                super->timer = (Random() & 63) | 96;
            }
            if (this->cooldown) {
                // a pause between bursts
                this->cooldown--;
                g = (this->cooldown & 0x10) ? 1 : 0;
            } else {
                this->step++;
                if ((this->step & 14) == 0) {
                    MoveVelWalls(super, this->targetX, this->targetY, 6);
                    if (++this->turns == this->head) {
                        this->turns = 0;
                        this->cooldown = (Random() & 31) + 64;
                        this->head = (Random() & 31) | 16;
                    }
                }
                g = (this->step & 8) >> 3;
            }
            break;
    }
    z = this->homeX >> 4;
    SetHeight(super, z);
    SetFrame(super, AF_ZOL_0 + g * 2 + (this->targetX < 0 ? 0 : 1));
    AlttpEndTick(this);
}

static void ZolOnCollision(AlttpEnemy* this);
static void ZolOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, CHUCHU);
}

static void (*const Zol_Functions[])(AlttpEnemy*) = {
    ZolTick, ZolOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    ZolOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void ZolOnCollision(AlttpEnemy* this) {
    AlttpOnCollision(this, (void (*const*)())Zol_Functions);
}

void Zol(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Zol_Functions);
}

/* ---- Firesnake ----------------------------------------------------------------------
 * Skull Woods' living fireball. It runs straight until a wall turns it left or right,
 * dropping a trail of fire behind it (type 1, which burns out). Nothing hurts it.
 */
void Firesnake(AlttpEnemy* this) {
    static const s8 velX[4] = { 24, -24, 0, 0 };
    static const s8 velY[4] = { 0, 0, 24, -24 };
    static const u8 turn[8] = { 2, 3, 2, 3, 0, 1, 0, 1 };
    if (super->action == 0) {
        AlttpInit(this, AP_FIRESNAKE, AF_FIRESNAKE_0);
        super->health = 0;
        super->spriteSettings.shadow = 0;
        if (super->type == 1) {
            COLLISION_OFF(super);
            super->timer = 32;
            super->spritePriority.b1 = 2;
        } else {
            this->face = Random() & 3;
        }
    }
    this->step++;
    super->knockbackDuration = 0;
    super->health = 0;
    if (super->type == 1) {
        if (--super->timer == 0) {
            DeleteThisEntity();
        }
        super->spriteSettings.draw = super->timer > 8 || (super->timer & 1);
        SetFrame(super, AF_FIRESNAKE_0 + ((this->step >> 2) & 3));
        return;
    }
    if (MoveVelWalls(super, velX[this->face], velY[this->face], 7)) {
        this->face = turn[this->face * 2 + (Random() & 1)];
    }
    if ((this->step & 7) == 0) {
        SpawnAt(FIRESNAKE, 1, super, 0, 0);
    }
    SetFrame(super, AF_FIRESNAKE_0 + ((this->step >> 2) & 3));
}

/* ---- Medusa and the four-way shooter ------------------------------------------------
 * Statues in the walls. Type 0 (Medusa) spits a fireball at Link every couple of
 * seconds; type 1 (the four-way shooter) answers a swing of Link's sword with a
 * fireball along whichever of its four sides faces him. Invisible: the statue is
 * the wall's own tile.
 */
void AlttpShooter(AlttpEnemy* this) {
    static const s8 offX[4] = { 0, 12, 0, -12 };
    static const s8 offY[4] = { -12, 0, 12, 0 };
    u32 dir, side;
    Entity* fb;
    if (super->action == 0) {
        super->action = 1;
        super->spriteSettings.draw = 0;
        super->health = 0;
        COLLISION_OFF(super);
    }
    this->step++;
    if (super->type == 0) {
        if ((this->step & 0x7f) != 0 || !InRoom(&gPlayerEntity.base, 0)) {
            return;
        }
        dir = GetFacingDirection(super, &gPlayerEntity.base);
        side = ((dir + 4) & 0x1f) >> 3;
        fb = SpawnAt(ALTTP_FIREBALL, 0, super, offX[side], offY[side]);
        if (fb != NULL) {
            fb->direction = dir;
            EnqueueSFX(SFX_ITEM_LANTERN_ON);
        }
        return;
    }
    if (super->timer) {
        if (--super->timer == 8) {
            side = ((GetFacingDirection(super, &gPlayerEntity.base) + 4) & 0x1f) >> 3;
            fb = SpawnAt(ALTTP_FIREBALL, 0, super, offX[side], offY[side]);
            if (fb != NULL) {
                fb->direction = side << 3;
                fb->speed = 0x280;
                EnqueueSFX(SFX_ITEM_LANTERN_ON);
            }
        }
    } else if (gPlayerState.sword_state != 0) {
        super->timer = 32;
    }
}

/* ---- the Helmasaur King --------------------------------------------------------------
 * The Palace of Darkness's boss: a great armored beast lumbering back and forth,
 * its spiked tail swaying over its back. Now and then it stops to spit a fireball
 * that splits in three and then in four, or to whip its tail out at Link. Its mask
 * turns the sword; the Hammer and bombs chip it away, and then it is soft. Without
 * its mask it moves twice as fast.
 *
 * Its parts (BOSS_PART): type 0 the lower body (with the mouth), 1 a leg, 2 the face
 * (mask and eyes), 3 a ball of the tail, 4 the mace, 5 a piece of the mask.
 */
#define HK_BALLS 6
#define HK_MASK_HP 12

typedef struct {
    s16 x[HK_BALLS + 1], y[HK_BALLS + 1]; /**< the tail, mace last */
    u8 bob[4];   /**< how far each leg is lifted */
    u8 mask;     /**< 0 whole, 1 and 2 chipped, 3 gone */
    u8 eye;
    u8 mouth;    /**< 0 shut, 1-10 open */
    u8 swiping;  /**< sprite_anim_clock: 0, 1 armed, 2 out, 3 back */
    u16 angle;   /**< the tail's sway */
    s16 swipe;   /**< added to the sway while it swipes */
    s8 vel;
    u8 swayDir;
    u8 radius;
    u8 swipeSide;
    u8 swipeWait;
    u8 eyeClock;
    u8 dead;
} HkState;
#define gHk (*(HkState*)(gAlttpEnemyState + 0xa00))

static const u8 sHkBob[32] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 8, 8, 8, 8, 8, 8,
                               8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 6, 5, 4, 3, 2, 1 };

enum { HKP_LOW, HKP_LEG, HKP_FACE, HKP_BALL, HKP_MACE, HKP_BIT };

static void HkSpawnBit(AlttpEnemy* this, u32 i) {
    static const s8 bx[10] = { -16, 0, 16, -16, 0, 16, -8, 8, -16, 16 };
    static const s8 by[10] = { 24, 27, 24, 24, 27, 24, 27, 27, 24, 24 };
    static const s8 bz[10] = { 29, 32, 29, 13, 16, 13, 0, 0, 13, 13 };
    static const s8 bvx[10] = { -16, -4, 14, -12, 4, 18, -2, 2, -12, 18 };
    static const s8 bvy[10] = { -8, -4, -6, 4, 2, 7, 6, 8, 4, 7 };
    static const s8 bvz[10] = { 32, 40, 36, 37, 39, 34, 30, 33, 37, 34 };
    static const u8 flip[10] = { 0, 0, 1, 0, 0, 1, 0, 1, 0, 1 };
    static const u8 gfx[10] = { 0, 1, 0, 2, 3, 2, 4, 4, 5, 5 };
    AlttpEnemy* bit = (AlttpEnemy*)SpawnAt(BOSS_PART, HKP_BIT, super, bx[i], by[i]);
    if (bit != NULL) {
        bit->head = gfx[i] * 2 + flip[i];
        bit->targetX = bvx[i];
        bit->targetY = bvy[i];
        bit->homeX = bz[i] << 4; // height, 1/16 pixels
        bit->homeY = bvz[i];
    }
}

/** The tail swaying over its back, and the swipe (HelmasaurKing_SwingTail). */
static void HkSwingTail(AlttpEnemy* this) {
    static const u8 multA[32] = { 0xff, 0xf0, 0xe0, 0xd0, 0xc0, 0xb0, 0xa0, 0x90, 0x80, 0x70, 0x60,
                                  0x50, 0x40, 0x30, 0x20, 0x10, 0xff, 0xf8, 0xf0, 0xe8, 0xe0, 0xd8,
                                  0xd0, 0xc8, 0xbc, 0xb0, 0xa0, 0x90, 0x70, 0x40, 0x20, 0x10 };
    // which of A Link to the Past's 16 tail balls are drawn: the mace, then six
    static const u8 balls[HK_BALLS + 1] = { 13, 11, 9, 7, 5, 3, 0 };
    u32 n;
    if (!(gHk.swiping == 0 && (this->step & 1))) {
        u32 j = gHk.swayDir & 1;
        gHk.vel += j ? -1 : 1;
        if (gHk.vel == (j ? -16 : 16)) {
            gHk.swayDir++;
        }
        gHk.angle += gHk.vel;
    }
    if (gHk.swiping == 2) {
        gHk.swipe += gHk.swipeSide ? -4 : 4;
        if (gHk.swipe == (gHk.swipeSide ? -124 : 124)) {
            gHk.swiping = 3;
        }
        gHk.radius += 3;
    } else if (gHk.swiping == 3) {
        gHk.swipe += gHk.swipeSide ? 4 : -4;
        if (gHk.swipe == 0) {
            gHk.swiping = 0;
        }
        gHk.radius -= 3;
    } else if (gHk.swiping == 1) {
        if (gHk.vel == 0 && gHk.swipeWait == 0) {
            gHk.swipeSide = (gHk.angle >> 8) & 1;
            // the tail is on Link's side: lash out
            if ((u32)(gPlayerEntity.base.x.HALF.HI > super->x.HALF.HI) == gHk.swipeSide) {
                gHk.swiping = 2;
                EnqueueSFX(SFX_14C);
            }
        }
    }
    if (gHk.swipeWait) {
        gHk.swipeWait--;
    }
    for (n = 0; n <= HK_BALLS; n++) {
        u32 i = balls[n];
        u32 rs = (u16)(gHk.angle + gHk.swipe);
        bool32 neg = (rs >> 8) == 0;
        u32 r6 = ((u8)(neg ? -rs : rs) * multA[i + (gHk.swiping >= 2 ? 16 : 0)]) >> 8;
        u32 a = (rs & 0xff00) | (neg ? (r6 ^ 0xff) : r6);
        s32 r = (gHk.radius * (i == 0 ? 0xff : 0x100 - i * 16)) >> 8;
        gHk.x[n] = HalfSin(a, r);
        gHk.y[n] = HalfSin(a + 0x80, r) - 40;
    }
}

static void HkSpit(AlttpEnemy* this) {
    Entity* fb = SpawnAt(HELMASAUR_FIREBALL, 0, super, 0, 28);
    if (fb != NULL) {
        fb->timer = 32;
    }
}

/** Every fourth stop it either spits fire or pauses (and may swipe). */
static bool32 HkMaybeAttack(AlttpEnemy* this) {
    if (++this->turns != 4) {
        return FALSE;
    }
    this->turns = 0;
    if (Random() & 1) {
        this->targetY = 127; // spitting
        EnqueueSFX(SFX_14C);
    } else {
        this->targetX = 160; // pausing
    }
    return TRUE;
}

static void HkArmSwipe(void) {
    if (gHk.swiping == 0) {
        gHk.swiping = 1;
        gHk.swipeWait = 32;
    }
}

static void HelmasaurKingTick(AlttpEnemy* this) {
    static const s8 velX[8] = { -12, -12, -4, 0, 4, 12, 12, 0 };
    static const s8 velY[8] = { 0, 4, 12, 12, 12, 4, 0, 12 };
    s16* pause = &this->targetX;
    s16* spit = &this->targetY;
    bool32 bare = gHk.mask >= 3;
    u32 i, n;
    if (super->action == 0) {
        static const s8 legX[4] = { -28, -28, 28, 28 };
        AlttpInit(this, AP_HK, AF_HKUP);
        BossStartDown(super, 12);
        MemClear(&gHk, sizeof(gHk));
        gHk.radius = 0x30;
        gHk.angle = 0x80;
        this->unused[0] = HK_MASK_HP;
        this->shield = 1;
        super->spriteSettings.shadow = 0;
        super->spritePriority.b0 = 3;
        super->timer = 64;
        *pause = *spit = 0;
        SpawnAt(BOSS_PART, HKP_LOW, super, 0, 0);
        SpawnAt(BOSS_PART, HKP_FACE, super, 0, 0);
        for (i = 0; i < 4; i++) {
            Entity* leg = SpawnAt(BOSS_PART, HKP_LEG, super, legX[i], 0);
            if (leg != NULL) {
                leg->type2 = i;
            }
        }
        for (i = 0; i <= HK_BALLS; i++) {
            Entity* ball = SpawnAt(BOSS_PART, i == HK_BALLS ? HKP_MACE : HKP_BALL, super, 0, -40);
            if (ball != NULL) {
                ball->type2 = i;
            }
        }
        super->action = 1;
    }
    this->step++;
    {
        // the eyes roll (kHelmasaurKing_DrawB_Char, by the tail's clock)
        static const u8 eyes[8] = { 0, 1, 2, 2, 2, 2, 1, 0 };
        gHk.eye = eyes[(++gHk.eyeClock >> 2) & 7];
    }
    HkSwingTail(this);
    gHk.mouth = 0;
    if (*pause) {
        if (--*pause == 96) {
            HkArmSwipe();
        }
        goto done;
    }
    if (*spit) {
        static const u8 open[32] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 10, 10, 10, 10, 10,
                                     10, 10, 10, 10, 10, 10, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1 };
        gHk.mouth = open[*spit >> 2];
        if (--*spit == 0x40) {
            HkSpit(this);
            if (bare) {
                HkArmSwipe();
            }
        }
        goto done;
    }
    if (super->timer) {
        super->timer--;
    }
    switch (super->action) {
        case 1:
            if ((super->iframes != 0 || super->timer == 0) && !HkMaybeAttack(this)) {
                u32 j = Random() & 7;
                this->homeX = velX[j];
                this->homeY = velY[j];
                super->timer = 64;
                if (bare) {
                    this->homeX <<= 1;
                    this->homeY <<= 1;
                    super->timer = 32;
                }
                super->action = 2;
            }
            break;
        case 2:
        case 4:
            n = 1 + ((this->step & 3) == 0) + bare;
            while (n--) {
                if ((++this->cooldown & 15) == 0) {
                    EnqueueSFX(SFX_THUD_HEAVY);
                }
            }
            MoveVelWalls(super, this->homeX, this->homeY, 30);
            if (super->timer == 0) {
                if (super->action == 2) {
                    super->timer = 32;
                    super->action = 3;
                } else {
                    super->timer = 64;
                    super->action = 1;
                }
            }
            break;
        case 3:
            if ((super->iframes != 0 || super->timer == 0) && !HkMaybeAttack(this)) {
                super->timer = bare ? 32 : 64;
                this->homeX = -this->homeX;
                this->homeY = -this->homeY;
                super->action = 4;
            }
            break;
    }
done:
    for (i = 0; i < 4; i++) {
        gHk.bob[i] = sHkBob[(this->cooldown + i * 8) & 31];
    }
    AlttpEndTick(this);
}

static void HelmasaurKingOnCollision(AlttpEnemy* this);
static void HelmasaurKingOnDeath(AlttpEnemy* this) {
    gHk.dead = 1;
    if (BossBurst(this, 16, 12)) {
        DeleteThisEntity();
    }
}

static void (*const HelmasaurKing_Functions[])(AlttpEnemy*) = {
    HelmasaurKingTick, HelmasaurKingOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    HelmasaurKingOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void HelmasaurKingOnCollision(AlttpEnemy* this) {
    Entity* by = super->contactedEntity;
    super->knockbackDuration = 0;
    if (super->health < this->savedHealth || (IsBlast(by) && gHk.mask < 3)) {
        if (gHk.mask < 3) {
            RestoreHealth(this);
            if (IsHeavy(by) || IsBlast(by)) {
                // a chip off the mask
                u32 hp = this->unused[0];
                u32 m;
                hp = hp > 2 ? hp - 2 : 0;
                this->unused[0] = hp;
                m = hp >= HK_MASK_HP ? 0 : hp >= 8 ? 1 : hp > 0 ? 2 : 3;
                if (m != gHk.mask) {
                    gHk.mask = m;
                    if (m == 3) {
                        for (m = 0; m < 8; m++) {
                            HkSpawnBit(this, m);
                        }
                        this->shield = 0;
                        EnqueueSFX(SFX_BOSS_HIT);
                    } else {
                        HkSpawnBit(this, gHk.mask + 7);
                    }
                }
                super->iframes = 16;
                EnqueueSFX(SFX_HAMMER3);
                BounceLink(super, 8);
            } else {
                super->iframes = -12;
                EnqueueSFX(SFX_METAL_CLINK);
            }
        } else {
            if (this->savedHealth - super->health > 4) {
                if (super->health == 0) {
                    COLLISION_ON(super);
                }
                super->health = this->savedHealth - 4;
            }
            EnqueueSFX(SFX_BOSS_HIT);
        }
    }
    EnemyFunctionHandlerAfterCollision(super, (void (*const*)())HelmasaurKing_Functions);
}

void HelmasaurKing(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)HelmasaurKing_Functions);
    BossCam(super, -16);
}

/** A Helmasaur King fireball. Type 0 is spat out, sinks and splits in three (type
 * 1), each of which splits in four (type 2) that fly off. */
void HelmasaurFireball(AlttpEnemy* this) {
    static const s8 triX[3] = { 0, 28, -28 };
    static const s8 triY[3] = { -32, 24, 24 };
    static const u8 triDelay[6] = { 32, 80, 128, 32, 80, 128 };
    static const s8 quadX[4] = { 32, 32, -32, -32 };
    static const s8 quadY[4] = { -32, 32, -32, 32 };
    u32 i, big = 1;
    if (super->action == 0) {
        AlttpInit(this, AP_HKFIRE, AF_HKFIRE_BIG);
        super->spriteSettings.shadow = 0;
        super->spritePriority.b0 = 2;
        super->action = 1;
        this->step = 0;
        if (super->type == 0) {
            this->targetX = this->targetY = 0;
        }
    }
    this->step++;
    super->health = 255;
    switch (super->type) {
        case 0:
            if (super->action == 1) {
                // a moment in the mouth, then it drops
                if (--super->timer == 0) {
                    super->action = 2;
                    super->timer = 18;
                    this->targetY = 36;
                }
            } else if (super->action == 2) {
                this->targetY -= 2;
                MoveVel(super, 0, this->targetY);
                if (--super->timer == 0) {
                    super->action = 3;
                    super->timer = 31;
                }
            } else {
                static const u8 grow[4] = { 0, 0, 1, 1 };
                big = grow[super->timer >> 3];
                if (--super->timer == 0) {
                    u32 r = Random() & 3;
                    EnqueueSFX(SFX_ITEM_BOMB_EXPLODE);
                    for (i = 0; i < 3; i++) {
                        AlttpEnemy* f = (AlttpEnemy*)SpawnAt(HELMASAUR_FIREBALL, 1, super, 0, 0);
                        if (f != NULL) {
                            f->targetX = triX[i];
                            f->targetY = triY[i];
                            f->base.timer = triDelay[r + i];
                            f->base.parent = NULL;
                        }
                    }
                    DeleteThisEntity();
                }
            }
            break;
        case 1:
            if (this->step <= 20) {
                MoveVel(super, this->targetX, this->targetY);
            }
            if (--super->timer == 0) {
                EnqueueSFX(SFX_ITEM_BOMB_EXPLODE);
                for (i = 0; i < 4; i++) {
                    AlttpEnemy* f = (AlttpEnemy*)SpawnAt(HELMASAUR_FIREBALL, 2, super, 0, 0);
                    if (f != NULL) {
                        f->targetX = quadX[i];
                        f->targetY = quadY[i];
                        f->base.parent = NULL;
                    }
                }
                DeleteThisEntity();
            }
            break;
        default:
            MoveVel(super, this->targetX, this->targetY);
            if (!InRoom(super, 8) || this->step > 240) {
                DeleteThisEntity();
            }
            break;
    }
    SetFrame(super, (big ? AF_HKFIRE_BIG : AF_HKFIRE_SMALL) + ((this->step >> 2) & 1));
}

/* ---- the bosses' parts ------------------------------------------------------------- */

static const Hitbox sHkBallHitbox = { 0, 0, { 4, 4, 4, 4 }, 6, 6 };

void BossPart(AlttpEnemy* this) {
    static const s8 legX[4] = { -28, -28, 28, 28 };
    static const s8 legY[4] = { -28, 4, -28, 4 };
    Entity* boss = super->parent;
    u32 i = super->type2;
    if (super->type == HKP_BIT) {
        // a piece of the mask flying off, bouncing once, and gone
        if (super->action == 0) {
            AlttpInit(this, AP_HKMASK, AF_HKBIT_0 + this->head);
            COLLISION_OFF(super);
            super->health = 0;
            super->spritePriority.b0 = 2;
            super->timer = 0;
        }
        MoveVel(super, this->targetX, this->targetY);
        this->homeX += this->homeY;
        this->homeY -= 2;
        if (this->homeX < 0) {
            this->homeX = 0;
            if (super->timer == 0) {
                super->timer = 12;
                this->homeY = 24;
            }
        }
        if (super->timer && --super->timer == 0) {
            DeleteThisEntity();
        }
        SetHeight(super, this->homeX >> 4);
        if ((this->step++ & 7) == 0) {
            this->head ^= 1; // tumbling
        }
        SetFrame(super, AF_HKBIT_0 + this->head);
        return;
    }
    if (!ParentAlive(super)) {
        DeleteThisEntity();
    }
    if (super->action == 0) {
        super->action = 1;
        COLLISION_OFF(super);
        super->health = 0;
        super->spriteSettings.shadow = 0;
        switch (super->type) {
            case HKP_LOW:
                AlttpInit(this, AP_HK, AF_HKLOW_0);
                super->spritePriority.b0 = 3;
                break;
            case HKP_LEG:
                AlttpInit(this, AP_HK, i < 2 ? AF_HKLEG_A : AF_HKLEG_A_FLIP);
                super->spritePriority.b0 = 3;
                break;
            case HKP_FACE:
                AlttpInit(this, AP_HKFACE, AF_HKFACE_0_0);
                super->spritePriority.b0 = 2;
                break;
            default:
                AlttpInit(this, AP_HK, super->type == HKP_MACE ? AF_HKTAIL_MACE : AF_HKTAIL_BALL);
                super->spritePriority.b0 = 1;
                super->hitbox = (Hitbox*)&sHkBallHitbox;
                break;
        }
        super->action = 1;
        super->health = 0;
    }
    super->x.HALF.HI = boss->x.HALF.HI;
    super->y.HALF.HI = boss->y.HALF.HI;
    super->collisionLayer = boss->collisionLayer;
    super->spriteSettings.draw = boss->spriteSettings.draw;
    switch (super->type) {
        case HKP_LOW:
            SetFrame(super, AF_HKLOW_0 + gHk.mouth);
            break;
        case HKP_LEG:
            super->x.HALF.HI += legX[i];
            super->y.HALF.HI += legY[i] + gHk.bob[i];
            SetFrame(super, (i & 1 ? AF_HKLEG_B : AF_HKLEG_A) + (i >= 2));
            break;
        case HKP_FACE:
            SetFrame(super, AF_HKFACE_0_0 + gHk.mask * 3 + gHk.eye);
            break;
        default:
            super->x.HALF.HI += gHk.x[i];
            super->y.HALF.HI += gHk.y[i];
            // the tail only hurts while it lashes out
            if (gHk.swiping >= 2 && !gHk.dead) {
                COLLISION_ON(super);
            } else {
                COLLISION_OFF(super);
            }
            super->knockbackDuration = 0;
            break;
    }
    super->health = 0;
}

/* ---- Arrghus ---------------------------------------------------------------------------
 * The Swamp Palace's boss: a great eye floating in a ring of thirteen puffs (the
 * Arrghi), which shield it. It drifts after Link, and every fourth stop spins its
 * puffs out wide and draws them in again. The Hookshot pulls a puff free, and a
 * loose puff can be beaten (it drifts back to the ring if left alone). With every
 * puff gone Arrghus leaps out of sight, crashes down on Link, and skitters wildly
 * over the water; now the sword hurts it, and each hit sends it up again.
 */
#define ARRGHI_COUNT 13

typedef struct {
    s16 x[ARRGHI_COUNT], y[ARRGHI_COUNT]; /**< each puff's place in the ring */
    u16 angle;
    u8 spin;
    u8 spread;  /**< overlord_x_lo[2]: the ring opening wider */
    u8 stops;
    u8 wobbleA, wobbleB;
    u8 puffs;   /**< puffs left */
} ArrghusState;
#define gArrghus (*(ArrghusState*)(gAlttpEnemyState + 0xb00))

enum { AR_INIT, AR_APPROACH, AR_SLOW, AR_SPREAD, AR_LEAP, AR_DROP, AR_SKITTER };

/** The ring of puffs (Arrghus_HandlePuffs). */
static void ArrghusPuffs(AlttpEnemy* this, u32 frame) {
    static const u16 base[ARRGHI_COUNT] = { 0, 0x40, 0x80, 0xc0, 0x100, 0x140, 0x180, 0x1c0, 0, 0x66, 0xcc, 0x132, 0x198 };
    static const u8 rev[ARRGHI_COUNT] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 };
    static const u8 ring[ARRGHI_COUNT] = { 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0xc, 0xc, 0xc, 0xc, 0xc };
    static const s8 wobble[52] = { 0, -1, -2, -3, -4, -5, -6, -6, -5, -4, -3, -2, -1, 0, -1, -2, -3, -4,
                                   -5, -6, -6, -5, -4, -3, -2, -1, 0, -1, -2, -3, -4, -5, -6, -6, -5, -4,
                                   -3, -2, -1, 0, -1, -2, -3, -4, -5, -6, -6, -5, -4, -3, -2, -1 };
    u32 i;
    gArrghus.angle += gArrghus.spin;
    if ((frame & 3) == 0 && ++gArrghus.wobbleA == 13) {
        gArrghus.wobbleA = 0;
    }
    if ((frame & 7) == 0 && ++gArrghus.wobbleB == 13) {
        gArrghus.wobbleB = 0;
    }
    for (i = 0; i < ARRGHI_COUNT; i++) {
        u32 a = (gArrghus.angle + base[i]) ^ (rev[i] ? 0x1ff : 0);
        s32 r = gArrghus.spread + ring[i];
        gArrghus.x[i] = super->x.HALF.HI + HalfSin(a, r + wobble[gArrghus.wobbleA + i]);
        gArrghus.y[i] = super->y.HALF.HI + HalfSin(a + 0x80, r + wobble[gArrghus.wobbleB + i]) - 16;
    }
}

static void ArrghusTick(AlttpEnemy* this) {
    static const u8 gfx[9] = { 1, 1, 1, 2, 2, 1, 1, 0, 0 };
    s16* vx = &this->targetX;
    s16* vy = &this->targetY;
    s32 z = -super->z.HALF.HI;
    u32 i;
    if (super->action == AR_INIT) {
        AlttpInit(this, AP_ARRGHUS, AF_ARRGHUS_0);
        MemClear(&gArrghus, sizeof(gArrghus));
        this->shield = 1;
        super->spritePriority.b0 = 3;
        super->collisionFlags |= 0x10;
        super->action = AR_APPROACH;
        super->timer = 48;
        *vx = *vy = this->homeX = this->homeY = 0;
        z = 24;
        ArrghusPuffs(this, 0);
        for (i = 0; i < ARRGHI_COUNT; i++) {
            Entity* puff = CreateEnemy(ARRGHI, i);
            if (puff != NULL) {
                puff->parent = super;
                puff->x.HALF.HI = gArrghus.x[i];
                puff->y.HALF.HI = gArrghus.y[i];
                puff->collisionLayer = super->collisionLayer;
                gArrghus.puffs++;
            }
        }
    }
    this->step++;
    gArrghus.spin = 1;
    if ((this->step & 3) == 0 && ++this->turns == 9) {
        this->turns = 0;
    }
    switch (super->action) {
        case AR_APPROACH:
            if (--super->timer == 0) {
                super->action = AR_SLOW;
                super->timer = 48;
            }
            ApproachVel(vx, this->homeX);
            ApproachVel(vy, this->homeY);
            break;
        case AR_SLOW:
            if (--super->timer == 0) {
                super->action = AR_APPROACH;
                if (gArrghus.puffs == 0) {
                    super->action = AR_LEAP;
                    EnqueueSFX(SFX_14C);
                } else if (++gArrghus.stops == 4) {
                    gArrghus.stops = 0;
                    super->action = AR_SPREAD;
                    super->timer = 176;
                } else {
                    super->timer = (Random() & 63) + 48;
                    AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI,
                           (super->timer & 3) + 8, &this->homeX, &this->homeY);
                }
            } else {
                ApproachVel(vx, 0);
                ApproachVel(vy, 0);
            }
            break;
        case AR_SPREAD:
            gArrghus.spin = 8;
            *vx = *vy = 0;
            if (super->timer < 32) {
                if (gArrghus.spread == 0) {
                    super->action = AR_SLOW;
                    super->timer = 112;
                } else {
                    gArrghus.spread--;
                }
            } else if (super->timer < 96) {
                gArrghus.spread++;
            } else if (super->timer == 96 || (super->timer & 15) == 0) {
                EnqueueSFX(SFX_14C);
            }
            super->timer--;
            break;
        case AR_LEAP:
            COLLISION_OFF(super);
            *vx = *vy = 0;
            z += 7;
            if (z >= 224) {
                super->action = AR_DROP;
                super->timer = 64;
                super->x.HALF.HI = gPlayerEntity.base.x.HALF.HI;
                super->y.HALF.HI = gPlayerEntity.base.y.HALF.HI;
            }
            break;
        case AR_DROP:
            if (super->timer) {
                if (--super->timer == 1) {
                    EnqueueSFX(SFX_FALL_HOLE);
                }
            } else {
                z -= 7;
                if (z <= 0) {
                    z = 0;
                    CreateFx(super, FX_WATER_SPLASH, 0);
                    EnqueueSFX(SFX_THUD_HEAVY);
                    InitScreenShake(16, 1);
                    super->action = AR_SKITTER;
                    super->timer = 32;
                    *vx = *vy = 32;
                    COLLISION_ON(super);
                }
            }
            break;
        case AR_SKITTER:
            this->shield = 0;
            if (super->timer) {
                super->timer--;
            } else {
                u32 hit = MoveVelWalls(super, *vx, *vy, 12);
                if (hit & 1) {
                    *vx = -*vx;
                }
                if (hit & 2) {
                    *vy = -*vy;
                }
                if ((this->step & 7) == 0) {
                    EnqueueSFX(SFX_WATER_WALK);
                    CreateFx(super, FX_WATER_SPLASH, 0);
                }
            }
            break;
    }
    if (super->action <= AR_SPREAD) {
        // floating, its feelers drifting
        u32 hit;
        z = 24;
        hit = MoveVelWalls(super, *vx, *vy, 14);
        if (hit & 1) {
            *vx = 0;
        }
        if (hit & 2) {
            *vy = 0;
        }
    }
    SetHeight(super, z);
    super->hitbox = (Hitbox*)(z > 8 ? &sHitbox_ArrghusAir : &sHitbox_ArrghusGround);
    ArrghusPuffs(this, this->step);
    if (super->action == AR_SKITTER) {
        SetFrame(super, AF_ARRGHUS_SWIM_0 + gfx[this->turns] * 3);
    } else {
        SetFrame(super, AF_ARRGHUS_0 + gfx[this->turns] * 3 + ((this->step >> 3) & 1));
    }
    AlttpEndTick(this);
}

static void ArrghusOnCollision(AlttpEnemy* this);
static void ArrghusOnDeath(AlttpEnemy* this) {
    if (BossBurst(this, 8, 8)) {
        DeleteThisEntity();
    }
}

static void (*const Arrghus_Functions[])(AlttpEnemy*) = {
    ArrghusTick, ArrghusOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    ArrghusOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void ArrghusOnCollision(AlttpEnemy* this) {
    super->knockbackDuration = 0;
    if (!this->shield && super->health < this->savedHealth && super->health != 0) {
        // stung, it leaps up to come crashing down again
        super->action = AR_LEAP;
        EnqueueSFX(SFX_BOSS_HIT);
    }
    AlttpOnCollision(this, (void (*const*)())Arrghus_Functions);
}

void Arrghus(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Arrghus_Functions);
    BossCam(super, -8);
}

/** One of Arrghus's puffs (type: its place in the ring). */
enum { AG_INIT, AG_RING, AG_HOOKED, AG_LOOSE };

static void ArrghiTick(AlttpEnemy* this) {
    static const u8 gfx[8] = { 0, 1, 2, 2, 2, 2, 2, 1 };
    u32 i = super->type;
    if (!ParentAlive(super)) {
        DeleteThisEntity();
    }
    switch (super->action) {
        case AG_INIT:
            AlttpInit(this, AP_ARRGHUS, AF_ARRGHI_0);
            super->action = AG_RING;
            this->shield = 1;
            this->step = Random();
            super->spriteSettings.shadow = 0;
            break;
        case AG_RING:
            this->shield = 1;
            super->x.HALF.HI = gArrghus.x[i];
            super->y.HALF.HI = gArrghus.y[i];
            break;
        case AG_HOOKED: {
            Entity* hook = gBlendLink.hook;
            if (hook != NULL && gBlendLink.hookState != HOOK_NONE) {
                super->x.HALF.HI = hook->x.HALF.HI;
                super->y.HALF.HI = hook->y.HALF.HI;
            } else {
                super->action = AG_LOOSE;
                super->timer = 32;
                this->shield = 0;
            }
            break;
        }
        case AG_LOOSE:
            if (super->timer) {
                super->timer--;
            }
            if (((i ^ this->step) & 3) == 0) {
                AimVel(super, gArrghus.x[i], gArrghus.y[i], 4, &this->targetX, &this->targetY);
                if (Abs(super->x.HALF.HI - gArrghus.x[i]) < 8 && Abs(super->y.HALF.HI - gArrghus.y[i]) < 8) {
                    super->action = AG_RING;
                }
            }
            MoveVel(super, this->targetX, this->targetY);
            break;
    }
    SetFrame(super, AF_ARRGHI_0 + gfx[(++this->step >> 3) & 7]);
    AlttpEndTick(this);
}

static void ArrghiOnCollision(AlttpEnemy* this);
static void ArrghiOnDeath(AlttpEnemy* this) {
    if (gArrghus.puffs) {
        gArrghus.puffs--;
    }
    CreateFx(super, FX_DEATH, 0);
    DeleteThisEntity();
}

static void (*const Arrghi_Functions[])(AlttpEnemy*) = {
    ArrghiTick, ArrghiOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    ArrghiOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void ArrghiOnCollision(AlttpEnemy* this) {
    if (IsHook(super->contactedEntity) && super->action == AG_RING) {
        // caught on the Hookshot: pulled out of the ring
        RestoreHealth(this);
        super->action = AG_HOOKED;
        EnqueueSFX(SFX_14C);
    }
    super->knockbackDuration = 0;
    AlttpOnCollision(this, (void (*const*)())Arrghi_Functions);
}

void Arrghi(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Arrghi_Functions);
}

/* ---- Mothula ----------------------------------------------------------------------------
 * Skull Woods' boss: a giant moth. It rises, flutters about the room bobbing up and
 * down (every seventh flight straight at Link), and stops to fire three rings of
 * light. Spike blocks keep bursting out of the walls. Hurt, it shrugs off blows for
 * a moment and flies on.
 */
enum { MO_INIT, MO_WAIT, MO_RISE, MO_FLY, MO_FIRE };

static void MothulaFlap(AlttpEnemy* this) {
    static const u8 flap[4] = { 0, 1, 2, 1 };
    u32 j = (++this->step >> 2) & 3;
    if (j == 0 && (this->step & 3) == 0) {
        EnqueueSFX(SFX_14C);
    }
    this->head = flap[j];
}

static u32 CountChildren(Entity* parent, u32 id) {
    u32 i, n = 0;
    Entity* e;
    for (i = 0; i < ARRAY_COUNT(gEntityLists); i++) {
        LinkedList* list = &gEntityLists[i];
        for (e = list->first; (u32)e != (u32)list; e = e->next) {
            if (e->kind == ENEMY && e->id == id && e->parent == parent) {
                n++;
            }
        }
    }
    return n;
}

/** A spike block out of a wall: a random place along a random side of the room. */
static void MothulaSpike(AlttpEnemy* this) {
    static const s8 dirX[4] = { 1, -1, 0, 0 };
    static const s8 dirY[4] = { 0, 0, 1, -1 };
    u32 d = Random() & 3;
    s32 x = this->homeX + ((s32)(Random() & 127) - 64);
    s32 y = this->homeY + ((s32)(Random() & 127) - 64);
    s32 n;
    AlttpEnemy* sb;
    if (CountChildren(super, ALTTP_SPIKE_BLOCK) >= 3) {
        return;
    }
    if (d < 2) {
        y = (y & ~15) + 8;
        x = this->homeX;
    } else {
        x = (x & ~15) + 8;
        y = this->homeY;
    }
    // walk away from the way it will charge, to the wall
    for (n = 0; n < 16; n++) {
        if (GetCollisionDataAtWorldCoords(x - dirX[d] * 16, y - dirY[d] * 16, super->collisionLayer) != 0) {
            break;
        }
        x -= dirX[d] * 16;
        y -= dirY[d] * 16;
    }
    if (n == 16 || GetCollisionDataAtWorldCoords(x, y, super->collisionLayer) != 0) {
        return;
    }
    sb = (AlttpEnemy*)CreateEnemy(ALTTP_SPIKE_BLOCK, 1);
    if (sb != NULL) {
        sb->base.x.HALF.HI = x;
        sb->base.y.HALF.HI = y;
        sb->base.collisionLayer = super->collisionLayer;
        sb->base.parent = super;
        sb->face = d;
    }
}

static void MothulaBeams(AlttpEnemy* this) {
    static const s8 vx[3] = { -16, 0, 16 };
    static const s8 vy[3] = { 24, 32, 24 };
    u32 i;
    EnqueueSFX(SFX_ITEM_SWORD_BEAM);
    for (i = 0; i < 3; i++) {
        AlttpEnemy* b = (AlttpEnemy*)SpawnAt(MOTHULA_BEAM, 0, super, vx[i], super->z.HALF.HI + 3);
        if (b != NULL) {
            b->targetX = vx[i];
            b->targetY = vy[i];
            b->base.timer = 16;
            b->base.parent = NULL;
        }
    }
}

static void MothulaTick(AlttpEnemy* this) {
    static const s8 vel[10] = { -16, -12, 0, 12, 16, 12, 0, -12, -16, -12 };
    s32 z = -super->z.HALF.HI;
    if (super->action == MO_INIT) {
        AlttpInit(this, AP_MOTHULA, AF_MOTHULA_2);
        super->action = MO_WAIT;
        super->timer = 80;
        this->head = 2;
        this->homeX = super->x.HALF.HI;
        this->homeY = super->y.HALF.HI;
        this->burst = 0;
        this->unused[0] = 112; // spike timer
        this->unused[1] = 0;   // z speed
        this->targetX = this->targetY = 0;
        super->spritePriority.b0 = 3;
        super->collisionFlags |= 0x10;
    }
    if (this->cooldown) {
        this->cooldown--;
    }
    switch (super->action) {
        case MO_WAIT:
            if (--super->timer == 0) {
                super->action = MO_RISE;
            }
            break;
        case MO_RISE:
            z += (this->step & 1);
            MothulaFlap(this);
            if (z >= 24) {
                super->action = MO_FLY;
                this->turns = 128; // flight time
                super->timer = 64;
            }
            break;
        case MO_FLY: {
            s8* vz = (s8*)&this->unused[1];
            if (this->turns == 0) {
                super->action = MO_FIRE;
                super->timer = 63;
                break;
            }
            this->turns--;
            MothulaFlap(this);
            // bobbing up and down
            *vz += (this->unused[2] & 1) ? -1 : 1;
            if (*vz == ((this->unused[2] & 1) ? -16 : 16)) {
                this->unused[2]++;
            }
            this->unused[3] += *vz;
            z += (s8)this->unused[3] >> 4;
            this->unused[3] &= 15;
            if (z < 8) {
                z = 8;
            } else if (z > 40) {
                z = 40;
            }
            if (super->timer == 0) {
                if (++this->coerced == 7) {
                    this->coerced = 0;
                    AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI, 32, &this->targetX,
                           &this->targetY);
                    super->timer = 128;
                } else {
                    u32 j = Random() & 7;
                    this->targetX = vel[j + 2];
                    this->targetY = vel[j];
                    super->timer = (Random() & 31) + 64;
                }
            } else {
                super->timer--;
            }
            if (MoveVelWalls(super, this->targetX, this->targetY, 16)) {
                super->timer = 0;
            }
            break;
        }
        case MO_FIRE:
            if (--super->timer == 0) {
                super->action = MO_FLY;
                this->turns = (Random() & 31) | 64;
            } else {
                if (super->timer == 0x20) {
                    MothulaBeams(this);
                }
                MothulaFlap(this);
            }
            break;
    }
    if (super->action >= MO_RISE && --this->unused[0] == 0) {
        this->unused[0] = 64;
        MothulaSpike(this);
    }
    SetHeight(super, z);
    super->hitbox = (Hitbox*)(z > 8 ? &sHitbox_MothulaAir : &sHitbox_MothulaGround);
    // a wavering ripple of shadow under it in A Link to the Past; here its shadow
    super->spriteSettings.shadow = 2;
    SetFrame(super, AF_MOTHULA_0 + this->head);
    AlttpEndTick(this);
}

static void MothulaOnCollision(AlttpEnemy* this);
static void MothulaOnDeath(AlttpEnemy* this) {
    if (BossBurst(this, 12, 6)) {
        DeleteThisEntity();
    }
}

static void (*const Mothula_Functions[])(AlttpEnemy*) = {
    MothulaTick, MothulaOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    MothulaOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void MothulaOnCollision(AlttpEnemy* this) {
    super->knockbackDuration = 0;
    if (super->health < this->savedHealth) {
        if (this->cooldown) {
            BlockHit(this);
        } else {
            // it reels, shrugs off blows for a moment, and flies on
            this->cooldown = 32;
            super->action = MO_FLY;
            this->turns = 64;
            super->timer = 0;
            EnqueueSFX(SFX_BOSS_HIT);
        }
    }
    EnemyFunctionHandlerAfterCollision(super, (void (*const*)())Mothula_Functions);
}

void Mothula(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Mothula_Functions);
    BossCam(super, -8);
}

/** Mothula's beam: a ring of light flying out, flickering, gone at a wall. */
void MothulaBeam(AlttpEnemy* this) {
    if (super->action == 0) {
        AlttpInit(this, AP_MBEAM, AF_MBEAM_0);
        super->spriteSettings.shadow = 0;
        super->spritePriority.b0 = 2;
    }
    super->health = 255;
    this->step++;
    MoveVel(super, this->targetX, this->targetY);
    if (super->timer) {
        super->timer--;
    } else if (WallAt(super, 0, 0) || !InRoom(super, 0)) {
        CreateFx(super, FX_SPARKLE, 0);
        DeleteThisEntity();
    }
    SetFrame(super, AF_MBEAM_0 + (this->step & 1));
}
#include "item.h"

/* =============================================================================
 * The Ice Palace, Misery Mire, Turtle Rock and Thieves' Town: their enemies and
 * their bosses (sprites 333-335). Included at the end of alttpEnemies.c, so it
 * shares its helpers.
 * ========================================================================== */

const Hitbox gHitbox_Blind = { 0, -10, { 6, 4, 4, 6 }, 8, 14 };
const Hitbox gHitbox_Kholdstare = { 0, 0, { 8, 8, 8, 8 }, 11, 11 };
const Hitbox gHitbox_KholdShell = { 0, 0, { 16, 16, 16, 16 }, 38, 30 };
const Hitbox gHitbox_Vitreous = { 0, 0, { 8, 8, 8, 8 }, 13, 13 };
const Hitbox gHitbox_Trinexx = { 0, 0, { 6, 4, 4, 6 }, 12, 12 };
const Hitbox gHitbox_TrinexxShell = { 0, 0, { 16, 16, 16, 16 }, 38, 28 };

/** A shot from the Fire Rod (ITEM_FIRE_ROD) or the Ice Rod (ITEM_ICE_ROD). */
static bool32 IsRodShot(Entity* e, u32 item) {
    return e != NULL && e->kind == PLAYER_ITEM && e->id == PLAYER_ITEM_FIRE_ROD_PROJECTILE &&
           ((u8*)e)[0x68] == item;
}

/** Fire: the Fire Rod's shot, or Bombos cast on this enemy (blend.c's CastMedallion
 * keeps which medallion it was in gBlendLink.medallionUsed: 1 for Bombos). */
static bool32 IsFire(Entity* self, Entity* e) {
    return IsRodShot(e, ITEM_FIRE_ROD) || (e != NULL && e->kind == OBJECT && e->id == LINK_PARTNER &&
                                           e->type == 2 && e->child == self && gBlendLink.medallionUsed == 1);
}

/** Takes away every enemy this one made (a beaten boss's parts and shots). */
static void DeleteChildren(Entity* parent) {
    u32 i;
    for (i = 0; i < ARRAY_COUNT(gEntityLists); i++) {
        LinkedList* list = &gEntityLists[i];
        Entity* e = list->first;
        while ((u32)e != (u32)list) {
            Entity* next = e->next;
            if (e != parent && e->kind == ENEMY && e->parent == parent) {
                DeleteEntity(e);
            }
            e = next;
        }
    }
}

/** Sprite_DirectionToFaceLink: 0 right, 1 left, 2 down, 3 up, by the longer axis. */
static u32 Face4ToLink(Entity* e) {
    s32 dx = gPlayerEntity.base.x.HALF.HI - e->x.HALF.HI;
    s32 dy = gPlayerEntity.base.y.HALF.HI - e->y.HALF.HI;
    if (Abs(dx) >= Abs(dy)) {
        return dx >= 0 ? 0 : 1;
    }
    return dy >= 0 ? 2 : 3;
}

/** The ALttP facing (0 right, 1 left, 2 down, 3 up) Link looks along. */
static u32 LinkFace4(void) {
    return DirToFace(gPlayerEntity.base.animationState << 2);
}

/** A Link to the Past 16-step heading where 0 is west and 4 south (Blind's) as a
 * Minish Cap direction. */
static u32 WestHeadingToDir(u32 h) {
    return (24 - h * 2) & 0x1f;
}

/** MoveVelWalls with a different reach across and down. */
static u32 MoveVelWalls2(Entity* e, s32 vx, s32 vy, s32 rx, s32 ry) {
    u32 hit = 0;
    if (vx != 0) {
        if (WallAt(e, vx > 0 ? rx : -rx, 0)) {
            hit |= 1;
        } else {
            e->x.WORD += vx << 12;
        }
    }
    if (vy != 0) {
        if (WallAt(e, 0, vy > 0 ? ry : -ry)) {
            hit |= 2;
        } else {
            e->y.WORD += vy << 12;
        }
    }
    return hit;
}

/** A jump or fall: z and its speed in 1/16 pixels, gravity per frame, landing at 0.
 * Returns TRUE on landing. */
static bool32 MoveZ16(s16* z, s16* vz, s32 gravity, s32 minVz) {
    *z += *vz;
    if (*vz > minVz) {
        *vz -= gravity;
    }
    if (*z <= 0) {
        *z = 0;
        *vz = 0;
        return TRUE;
    }
    return FALSE;
}

/** A hit that got through: TRUE once per blow (the engine took health off). The
 * health is put back, for the bosses that count their blows themselves. */
static bool32 TookHit(AlttpEnemy* this) {
    if (super->health < this->savedHealth) {
        RestoreHealth(this);
        return TRUE;
    }
    return FALSE;
}

/* ---- shots and trails (ALTTP_BOLT) ------------------------------------------------
 * By type: Blind's laser, Vitreous's lightning, falling ice (Kholdstare's), Trinexx's
 * breath of ice and of fire and the flames it leaves, a Wizzrobe's beam, a Laser
 * Eye's laser, a Zazak's fireball, and the trails the beams leave (types 1, 3, 10:
 * one for each sprite). None of them can be hurt, and they never hold doors shut.
 */
enum {
    BT_BLINDLASER, BT_TRAIL7, BT_LIGHTNING, BT_TRAIL8, BT_ICE, BT_TICE, BT_TFIRE, BT_FLAME,
    BT_WIZZBEAM, BT_LASER, BT_TRAIL6, BT_PHLEGM,
};

/** Leaves a still copy of a beam's frame behind for a few frames. */
static void SpawnTrail(Entity* from, u32 type, u32 frame, u32 pal, u32 life) {
    AlttpEnemy* t = (AlttpEnemy*)CreateEnemy(ALTTP_BOLT, type);
    if (t != NULL) {
        CopyPosition(from, &t->base);
        t->base.parent = from->parent;
        t->targetX = frame;
        t->idx = pal;
        t->base.timer = life;
    }
}

static Entity* SpawnBolt(Entity* from, u32 type, s32 dx, s32 dy, s32 vx, s32 vy) {
    AlttpEnemy* b = (AlttpEnemy*)SpawnAt(ALTTP_BOLT, type, from, dx, dy);
    if (b != NULL) {
        b->targetX = vx;
        b->targetY = vy;
    }
    return &b->base;
}

void AlttpBolt(AlttpEnemy* this) {
    static const u8 boltPal[] = { AP_BLINDLASER, 0, AP_BOLT, 0, AP_ICEBALL, AP_ICEBALL, AP_KFIRE, AP_KFIRE,
                                  AP_WIZZBEAM, AP_LASER, 0, AP_PHLEGM };
    s16* z = &this->homeX;
    s16* vz = &this->homeY;
    if (super->action == 0) {
        u32 type = super->type;
        if (type == BT_TRAIL6 || type == BT_TRAIL7 || type == BT_TRAIL8) {
            AlttpInit(this, this->idx, this->targetX);
            if (type != BT_TRAIL8) {
                COLLISION_OFF(super);
            }
        } else {
            AlttpInit(this, boltPal[type], 0);
        }
        super->spriteSettings.shadow = 0;
        super->spritePriority.b0 = 2;
        this->step = 0;
        if (type == BT_ICE) {
            if (super->type2 == 0) {
                // high above where Link stands, its shadow showing where it lands
                *z = 160 << 4;
                *vz = 0;
                COLLISION_OFF(super);
                super->spriteSettings.shadow = 1;
                EnqueueSFX(SFX_FALL_HOLE);
            } else {
                *z = 0;
                *vz = 32;
            }
        }
    }
    super->health = 0;
    super->knockbackDuration = 0;
    this->step++;
    switch (super->type) {
        case BT_TRAIL6:
        case BT_TRAIL7:
        case BT_TRAIL8:
            if (--super->timer == 0) {
                DeleteThisEntity();
            }
            super->spriteSettings.draw = super->timer > 2 || (super->timer & 1);
            return;
        case BT_BLINDLASER:
            // eight pixels a frame along Blind's look, a trail of light behind it
            if (super->timer) {
                if (--super->timer == 0) {
                    DeleteThisEntity();
                }
                return;
            }
            super->x.HALF.HI += this->targetX;
            super->y.HALF.HI += this->targetY;
            SetFrame(super, AF_BLINDLASER_0 + this->idx);
            SpawnTrail(super, BT_TRAIL7, AF_BLINDLASER_0 + this->idx, AP_BLINDLASER, 6);
            if (ShotWallAt(super) || !InRoom(super, 0)) {
                super->timer = 6;
                COLLISION_OFF(super);
                super->spriteSettings.draw = 0;
                CreateFx(super, FX_SPARKLE2, 0);
            }
            return;
        case BT_LIGHTNING: {
            // Sprite_BF_Lightning: a fork every two frames, zigzagging down the room
            static const s8 xoff[32] = { -15, 0, 0, -15, 0, -15, -15, 0, 0, 15, 15, 0, 15, 0, 0, 15,
                                         0, 15, 15, 0, 15, 0, 0, 15, -15, 0, 0, -15, 0, -15, -15, 0 };
            u32 a = this->idx & 7, r;
            SetFrame(super, (a & 4 ? AF_BOLT_0_FLIP : AF_BOLT_0) + (a & 3) * 2);
            if (this->step & 1) {
                return;
            }
            SpawnTrail(super, BT_TRAIL8, super->animIndex, AP_BOLT, 10);
            super->y.HALF.HI += 16;
            if (super->y.HALF.HI > gRoomControls.origin_y + gRoomControls.height || this->step > 60) {
                DeleteThisEntity();
            }
            r = Random() & 7;
            super->x.HALF.HI += xoff[((a >> 1) & 3) * 8 + r];
            this->idx = r;
            return;
        }
        case BT_ICE:
            // Sprite_A4_FallingIce: the block drops and bursts in four chunks, which
            // bounce off and shatter
            if (super->timer) {
                if (--super->timer == 0) {
                    DeleteThisEntity();
                }
                SetFrame(super, AF_ICEBALL_SPIN);
                return;
            }
            if (super->type2 == 0) {
                super->spriteSettings.draw = this->step & 1 ? 1 : *z < (96 << 4);
                if (MoveZ16(z, vz, 3, -64)) {
                    static const s8 qx[8] = { 0, 32, 0, -32, 24, 24, -24, -24 };
                    static const s8 qy[8] = { -32, 0, 32, 0, -24, 24, -24, 24 };
                    u32 b = Random() & 4, i;
                    EnqueueSFX(SFX_ICE_BLOCK_STOP);
                    CreateFx(super, FX_ICE, 0);
                    for (i = 0; i < 4; i++) {
                        AlttpEnemy* c = (AlttpEnemy*)SpawnBolt(super, BT_ICE, 0, 0, qx[i + b], qy[i + b]);
                        if (c != NULL) {
                            c->base.type2 = 1;
                            c->base.parent = super->parent;
                        }
                    }
                    DeleteThisEntity();
                }
                SetFrame(super, AF_ICEBALL_0);
            } else {
                MoveVel(super, this->targetX, this->targetY);
                if (MoveZ16(z, vz, 3, -64) || WallAt(super, 0, 0)) {
                    super->timer = 15;
                    EnqueueSFX(SFX_ICE_BLOCK_MELT);
                }
                SetFrame(super, AF_ICEBALL_1);
            }
            SetHeight(super, *z >> 4);
            return;
        case BT_TICE:
            // Sprite_CD: the ice head's breath, drifting toward Link as it falls
            if (!(this->step & 3)) {
                s32 m = gPlayerEntity.base.x.HALF.HI < super->x.HALF.HI ? -1 : 1;
                if (this->idx != (u8)(m * 16)) {
                    this->idx += m;
                }
            }
            MoveVel(super, (s8)this->idx + this->targetX, 24);
            if ((this->step & 7) == 0) {
                CreateFx(super, FX_BLUE_SPARKLE, 0);
            }
            if (WallAt(super, 0, 0) || !InRoom(super, 0) || this->step > 200) {
                CreateFx(super, FX_ICE, 0);
                DeleteThisEntity();
            }
            SetFrame(super, AF_ICEBALL_1 + ((this->step >> 2) & 1) * (AF_ICEBALL_SPIN - AF_ICEBALL_1));
            return;
        case BT_TFIRE:
            // Sprite_CC: the fire head's breath, leaving flames on the floor
            if (!(this->step & 3)) {
                s32 m = gPlayerEntity.base.x.HALF.HI < super->x.HALF.HI ? -1 : 1;
                if (this->targetX != m * 16) {
                    this->targetX += m;
                }
            }
            MoveVel(super, this->targetX, 24);
            if ((this->step & 15) == 0) {
                SpawnBolt(super, BT_FLAME, 0, 0, 0, 0);
                EnqueueSFX(SFX_ITEM_LANTERN_ON);
            }
            if (WallAt(super, 0, 0) || !InRoom(super, 0) || this->step > 200) {
                CreateFx(super, FX_DEATH, 0);
                DeleteThisEntity();
            }
            SetFrame(super, AF_KFIRE_0 + ((this->step >> 2) & 3));
            return;
        case BT_FLAME: {
            static const u8 burn[8] = { 5, 4, 3, 1, 2, 0, 3, 0 };
            if (this->step >= 64) {
                DeleteThisEntity();
            }
            SetFrame(super, AF_KFLAME_0 + burn[(63 - this->step) >> 3]);
            return;
        }
        case BT_WIZZBEAM:
            MoveVel(super, this->targetX, this->targetY);
            super->spriteSettings.draw = 1;
            SetFrame(super, AF_WIZZBEAM_0 + this->idx);
            break;
        case BT_LASER:
            MoveVel(super, this->targetX, this->targetY);
            SetFrame(super, AF_LASER_0 + (this->idx >> 1));
            SpawnTrail(super, BT_TRAIL6, AF_LASER_0 + (this->idx >> 1), AP_LASER, 8);
            if (this->step < 5) {
                return;
            }
            break;
        case BT_PHLEGM:
            MoveVel(super, this->targetX, this->targetY);
            SetFrame(super, AF_PHLEGM_0_0 + this->idx * 2 + ((this->step >> 1) & 1));
            break;
    }
    if (ShotWallAt(super) || !InRoom(super, 0) || this->step > 240) {
        CreateFx(super, FX_SPARKLE2, 0);
        DeleteThisEntity();
    }
}

/* ---- the bosses' pieces (BOSS_PIECE) ---------------------------------------------- */
enum { BP_BLINDHEAD, BP_FLYHEAD, BP_KPUPIL, BP_TLEGS, BP_TSHELL, BP_TNECK, BP_TSEG, BP_CHAIN };

/* ---- Blind -------------------------------------------------------------------------
 * Thieves' Town's boss, the thief who hid in the maiden's guise. He backs up to the
 * wall and sways along it, his head turning on its own and firing a laser from his
 * eyes when it finds Link; when Link gets behind him he crosses the room to the other
 * wall and whirls around. A blow makes him spin his head and spit a ring of
 * fireballs; every third, his head flies off (and keeps flying, spitting fire,
 * until he is beaten) and he hides behind the curtain to grow another. The third
 * head he loses is the end of him.
 *
 * Fields: face the wall (0 the top, looking down; 1 the bottom), head where it
 * looks (0 west, 4 south), pose the robes (kBlind_Dmd's graphics), turns the blows
 * since a head came off, coerced the heads lost, targetX/Y the speed, idx and crusher
 * the sway's turns (B and G), unused: [0] fireball flurry left, [1] [2] its spin,
 * [3] the head's clock; rangeX the laser's wind-up, rangeY frames before it fires
 * again, cooldown frames it shrugs off blows.
 */
enum { BL_INIT, BL_RETREAT, BL_SWAY, BL_SWITCH, BL_WHIRL, BL_CURTAIN, BL_REROBE, BL_MAIDEN };

static void BlindRobes(AlttpEnemy* this) {
    static const u8 gfx[8] = { 7, 8, 9, 8, 0, 1, 2, 1 };
    this->pose = gfx[((this->step >> 3) & 3) + (this->face << 2)];
}

static void BlindAnimate(AlttpEnemy* this) {
    static const u8 headDir[17] = { 0, 1, 2, 3, 4, 3, 2, 1, 0, 15, 14, 13, 12, 13, 14, 15, 0 };
    static const u8 tab[8] = { 0, 1, 1, 2, 2, 3, 3, 4 };
    if (this->unused[0] == 0) {
        s32 lx = gPlayerEntity.base.x.HALF.HI - this->homeX + 128;
        s32 t1;
        u32 c = this->unused[3];
        lx = lx < 0 ? 0 : lx > 255 ? 255 : lx;
        t1 = tab[lx >> 5];
        if (this->face) {
            t1 = -t1;
        }
        this->head = (headDir[((c >> 3) & 7) + ((c >> 2) & 1) + this->face * 8] + t1) & 15;
    }
    BlindRobes(this);
}

/** Blind_SpitFireball: a fireball the way the head looks, from the head. */
static Entity* BlindSpit(Entity* from, u32 head, s32 dy) {
    Entity* fb = SpawnAt(ALTTP_FIREBALL, 0, from, 0, dy);
    if (fb != NULL) {
        fb->direction = WestHeadingToDir(head);
        fb->collisionLayer = from->collisionLayer;
        EnqueueSFX(SFX_ITEM_LANTERN_ON);
    }
    return fb;
}

static void BlindFlurry(AlttpEnemy* this) {
    this->unused[0]--;
    if ((s8)--this->unused[1] < 0) {
        this->unused[1] = this->unused[2];
        this->head = (this->head + 1) & 15;
    }
    if ((this->step & 31) == 0 && this->unused[2] != 5) {
        this->unused[2]++;
    }
    BlindRobes(this);
    if ((this->step & 15) == 0) {
        BlindSpit(super, this->head, -12);
    }
}

static void BlindDecelerate(AlttpEnemy* this) {
    if (this->targetX != 0) {
        this->targetX += this->targetX < 0 ? 2 : -2;
    }
    if (this->targetY != 0) {
        this->targetY += this->targetY < 0 ? 4 : -4;
        if (Abs(this->targetY) < 4) {
            this->targetY = 0;
        }
    }
}

/** The look in his eyes: Link straight along it (Sprite_SpawnProbeAlways). */
static bool32 BlindSeesLink(AlttpEnemy* this) {
    s32 dx = gPlayerEntity.base.x.HALF.HI - super->x.HALF.HI;
    s32 dy = gPlayerEntity.base.y.HALF.HI - (super->y.HALF.HI - 12);
    u32 h = (AngleToHeading(AngleTo(dx, dy)) + 8) & 15;
    return h == this->head;
}

static void BlindLaser(AlttpEnemy* this) {
    static const s8 vx[16] = { -8, -8, -8, -4, 0, 4, 8, 8, 8, 8, 8, 4, 0, -4, -8, -8 };
    static const s8 vy[16] = { 0, 0, 4, 8, 8, 8, 4, 0, 0, 0, -4, -8, -8, -8, -4, 0 };
    AlttpEnemy* l = (AlttpEnemy*)SpawnBolt(super, BT_BLINDLASER, 0, -12, vx[this->head], vy[this->head]);
    if (l != NULL) {
        l->idx = this->head;
        EnqueueSFX(SFX_ITEM_SWORD_BEAM);
    }
}

/** The middle of Blind's room: the rooms here are not ALttP's, so his sway is
 * centred between the walls found either side of where he was placed. */
static s32 BlindScan(Entity* e, s32 sx, s32 sy) {
    s32 d;
    for (d = 8; d < 136; d += 8) {
        if (WallAt(e, sx * d, sy * d)) {
            return d;
        }
    }
    return 0;
}

static void BlindFindCenter(AlttpEnemy* this) {
    s32 l = BlindScan(super, -1, 0), r = BlindScan(super, 1, 0);
    s32 u = BlindScan(super, 0, -1), d = BlindScan(super, 0, 1);
    this->homeX = super->x.HALF.HI;
    this->homeY = super->y.HALF.HI;
    if (l && r) {
        this->homeX += (r - l) / 2;
    }
    if (u && d) {
        this->homeY += (d - u) / 2;
    }
    super->x.HALF.HI = this->homeX;
    super->y.HALF.HI = this->homeY;
}

/**
 * The maiden of Thieves' Town, who is Blind. In A Link to the Past she follows Link
 * up from her cell, and in this room the light from the window shows her for what
 * she is (Follower_CheckBlindTrigger, Blind_SpawnFromMaiden, then Blind's
 * "blinded" state: she stands, the boss poof, and Blind). Here she waits in the
 * room: she comes a few steps toward Link, and when he is near she stops, shudders
 * and stretches, and Blind tears out of her in a poof of smoke. Fields: timer the
 * transformation's countdown (0 until it starts), unused[0] her walk clock.
 */
static void BlindMaiden(AlttpEnemy* this) {
    s32 dx = gPlayerEntity.base.x.HALF.HI - super->x.HALF.HI;
    s32 dy = gPlayerEntity.base.y.HALF.HI - (super->y.HALF.HI + 32);
    u32 f = AF_MAIDEN_0;
    this->pose = 14; // no head of Blind's yet (BP_BLINDHEAD hides at poses 10 and up)
    super->spriteSettings.draw = 1;
    if (super->timer == 0) {
        if (Distance(dx, dy) > 48) {
            // her steps toward Link, as she followed him (a few, from where she waits)
            u32 side = Abs(dx) > Abs(dy);
            s32 sx = side ? (dx > 0 ? 1 : -1) : 0;
            s32 sy = side ? 0 : (dy > 0 ? 1 : -1);
            u32 step = (++this->unused[0] >> 3) & 1;
            if ((this->unused[0] & 1) && Abs(super->x.HALF.HI + sx - this->homeX) < 24 &&
                Abs(super->y.HALF.HI + sy - this->homeY) < 24 && !WallAt(super, sx * 12, 32 + sy * 12)) {
                super->x.HALF.HI += sx;
                super->y.HALF.HI += sy;
            }
            f = side ? (dx > 0 ? AF_MAIDEN_SIDE_2 : AF_MAIDEN_SIDE_0) + step
                     : (dy > 0 ? AF_MAIDEN_0 : AF_MAIDEN_UP_0) + step;
        } else {
            super->timer = 128;
            EnqueueSFX(SFX_EVAPORATE);
        }
    } else {
        u32 t = --super->timer;
        if (t >= 96) {
            f = AF_MAIDEN_0; // she stands, and the light finds her
        } else if (t >= 56) {
            f = (t & 4) ? AF_MAIDEN_SHAKE : AF_MAIDEN_0;
            if ((t & 15) == 0) {
                EnqueueSFX(SFX_14C);
            }
        } else if (t >= 24) {
            f = (t & 2) ? AF_MAIDEN_RISE : AF_MAIDEN_SHAKE;
            if (t == 40) {
                InitScreenShake(16, 0);
            }
        } else {
            // the boss poof: Blind tears out of her
            if (t == 23) {
                u32 i;
                static const s8 px[5] = { 0, -14, 14, -8, 8 };
                static const s8 py[5] = { 18, 10, 10, 30, 30 };
                for (i = 0; i < 5; i++) {
                    Entity* fx = CreateFx(super, FX_DEATH, 0);
                    if (fx != NULL) {
                        fx->x.HALF.HI = super->x.HALF.HI + px[i];
                        fx->y.HALF.HI = super->y.HALF.HI + py[i];
                    }
                }
                EnqueueSFX(SFX_SUMMON);
            }
            this->pose = 9;
            super->spriteSettings.draw = (t >> 1) & 1;
            f = AF_BLIND_9;
            if (t == 0) {
                super->action = BL_RETREAT;
                super->timer = 96;
                super->spriteSettings.draw = 1;
                COLLISION_ON(super);
                EnqueueSFX(SFX_BOSS_HIT);
            }
        }
    }
    SetFrame(super, f);
    AlttpEndTick(this);
}

static void BlindTick(AlttpEnemy* this) {
    static const s8 swayVy[2] = { 18, -18 };
    static const s8 swayVx[2] = { 24, -24 };
    static const s8 swayX[2] = { 44, -44 };
    s16* vx = &this->targetX;
    s16* vy = &this->targetY;
    u32 j;
    if (super->action == BL_INIT) {
        AlttpInit(this, AP_BLIND, AF_BLIND_9);
        BossStartDown(super, 24);
        ((Enemy*)this)->enemyFlags |= EM_FLAG_BOSS;
        // first the maiden he poses as (BlindMaiden), then the retreat
        super->action = BL_MAIDEN;
        super->timer = 0;
        COLLISION_OFF(super);
        super->spritePriority.b0 = 3;
        BlindFindCenter(this);
        this->face = 0;
        this->head = 4;
        this->pose = 9;
        this->shield = 1;
        this->turns = this->coerced = 0;
        this->unused[0] = this->unused[1] = this->unused[2] = this->unused[3] = 0;
        this->rangeX = this->rangeY = this->cooldown = 0;
        *vx = *vy = 0;
        {
            Entity* h = SpawnAt(BOSS_PIECE, BP_BLINDHEAD, super, 0, 0);
            if (h != NULL) {
                h->collisionLayer = super->collisionLayer;
            }
        }
    }
    if (super->action == BL_MAIDEN) {
        BlindMaiden(this);
        return;
    }
    this->step++;
    if (this->cooldown) {
        this->cooldown--;
    }
    if (this->rangeY) {
        this->rangeY--;
    }
    // the sway's timer runs at half speed (sprite_delay_main, nudged every other frame)
    if (super->subtimer) {
        super->subtimer--;
    }
    if (!(this->step & 1)) {
        super->subtimer++;
    }
    if (this->rangeX) {
        // a moment's stillness, and the laser
        if (--this->rangeX == 8) {
            BlindLaser(this);
        }
        goto done;
    }
    this->unused[3]++;
    switch (super->action) {
        case BL_RETREAT:
            this->pose = 9;
            this->head = 4;
            if (--super->timer == 0) {
                super->action = BL_SWAY;
                super->subtimer = 255;
                this->shield = 0;
            } else if (super->timer < 64 && super->y.HALF.HI > this->homeY - 32) {
                MoveVelWalls2(super, 0, -8, 18, 20);
            }
            BlindAnimate(this);
            this->head = 4;
            break;
        case BL_SWAY: {
            bool32 behind = this->face == 0 ? gPlayerEntity.base.y.HALF.HI < super->y.HALF.HI
                                            : gPlayerEntity.base.y.HALF.HI > super->y.HALF.HI;
            BlindAnimate(this);
            if ((((this->step & 127) == 0 && behind) || super->subtimer == 0) &&
                super->x.HALF.HI < this->homeX) {
                super->action = BL_SWITCH;
                *vx &= ~1;
                *vy &= ~1;
                super->timer = 0x30;
                break;
            }
            j = this->idx & 1;
            *vy += j ? -1 : 1;
            if (*vy == swayVy[j]) {
                this->idx++;
            }
            j = this->crusher & 1;
            if (*vx != swayVx[j]) {
                *vx += j ? -1 : 1;
            }
            if (j == 0 ? super->x.HALF.HI >= this->homeX + swayX[0] : super->x.HALF.HI <= this->homeX + swayX[1]) {
                this->crusher++;
            }
            {
                // the sway stays inside the walls, and turns back at one
                u32 hit = MoveVelWalls2(super, *vx, *vy, 18, 20);
                if ((hit & 1) && ((*vx > 0) == (j == 0))) {
                    this->crusher++;
                }
                if (hit & 2) {
                    *vy = -*vy;
                    this->idx++;
                }
            }
            if (this->unused[0]) {
                BlindFlurry(this);
            } else if (!(this->step & 7) && !this->rangeY && BlindSeesLink(this)) {
                this->rangeX = 16;
                this->rangeY = 128;
            }
            break;
        }
        case BL_SWITCH:
            if (super->timer) {
                super->timer--;
                BlindDecelerate(this);
                MoveVelWalls2(super, *vx, *vy, 18, 20);
            } else {
                s32 target = this->homeY + (this->face == 0 ? 32 : -32);
                s32 want = this->face == 0 ? 64 : -64;
                if (*vy != want) {
                    *vy += this->face == 0 ? 2 : -2;
                }
                if (*vx != 0) {
                    *vx += *vx < 0 ? 2 : -2;
                }
                if ((MoveVelWalls2(super, *vx, *vy, 18, 20) & 2) ||
                    (this->face == 0 ? super->y.HALF.HI >= target : super->y.HALF.HI <= target)) {
                    if (super->y.HALF.HI * (this->face == 0 ? 1 : -1) > target * (this->face == 0 ? 1 : -1)) {
                        super->y.HALF.HI = target;
                    }
                    super->action = BL_WHIRL;
                    this->idx = this->face == 0 ? 0 : 1;
                    *vy = 0;
                }
            }
            if (this->unused[0]) {
                BlindFlurry(this);
            } else {
                BlindRobes(this);
            }
            break;
        case BL_WHIRL:
            // Blind_WhirlAround: the robes swing round, a pose every eight frames
            if (!(this->step & 7)) {
                u32 goal = this->face == 0 ? 0 : 9;
                if (this->pose == goal) {
                    super->subtimer = 254;
                    super->action = BL_SWAY;
                    this->face ^= 1;
                    this->crusher = super->x.HALF.HI >= this->homeX;
                } else if (this->pose > goal) {
                    this->pose--;
                } else {
                    this->pose++;
                }
            }
            if (*vy) {
                *vy += *vy < 0 ? 4 : -4;
            }
            this->head = this->face == 0 ? 4 : 12;
            break;
        case BL_CURTAIN:
            this->head = 12;
            if (--super->timer == 0) {
                super->action = BL_REROBE;
                super->timer = 39;
                EnqueueSFX(SFX_SUMMON);
            } else if (super->timer >= 224) {
                static const u8 curtain[4] = { 14, 13, 12, 10 };
                this->pose = curtain[(super->timer - 224) >> 3];
            } else {
                this->pose = 14;
            }
            break;
        case BL_REROBE:
            if (super->timer == 0 || --super->timer == 0) {
                super->action = BL_SWAY;
                super->subtimer = 128;
                this->face = super->y.HALF.HI > this->homeY;
                this->crusher = super->x.HALF.HI >= this->homeX;
                *vx = *vy = 0;
                this->shield = 0;
            } else {
                static const u8 rerobe[5] = { 10, 11, 12, 13, 14 };
                this->pose = rerobe[super->timer >> 3];
            }
            break;
    }
done:
    SetFrame(super, AF_BLIND_0 + this->pose);
    AlttpEndTick(this);
}

static void BlindOnCollision(AlttpEnemy* this);
static void BlindOnDeath(AlttpEnemy* this) {
    if (this->burst == 0) {
        DeleteChildren(super);
    }
    if (BossBurst(this, 8, 12)) {
        DeleteThisEntity();
    }
}

static void (*const Blind_Functions[])(AlttpEnemy*) = {
    BlindTick, BlindOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    BlindOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void BlindOnCollision(AlttpEnemy* this) {
    super->knockbackDuration = 0;
    if (TookHit(this)) {
        if (this->shield || this->cooldown) {
            EnqueueSFX(SFX_METAL_CLINK);
        } else {
            this->cooldown = 48;
            super->iframes = 24;
            EnqueueSFX(SFX_BOSS_HIT);
            if (++this->turns < 3) {
                // stung: his head spins and spits fire all round
                this->unused[0] = 96;
                this->unused[2] = 1;
            } else {
                this->turns = 0;
                if (++this->coerced == 3) {
                    super->health = 0;
                } else {
                    // his head flies off, and he hides behind the curtain to grow another
                    AlttpEnemy* h = (AlttpEnemy*)SpawnAt(BOSS_PIECE, BP_FLYHEAD, super, 0, -12);
                    if (h != NULL) {
                        h->head = this->head;
                        h->base.collisionLayer = super->collisionLayer;
                        h->homeX = this->homeX;
                        h->homeY = this->homeY;
                    }
                    this->targetX = this->targetY = 0;
                    this->unused[0] = 0;
                    super->action = BL_CURTAIN;
                    super->timer = 255;
                    this->shield = 1;
                }
            }
        }
    }
    EnemyFunctionHandlerAfterCollision(super, (void (*const*)())Blind_Functions);
}

void Blind(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Blind_Functions);
    BossCam(super, -16);
}

/* ---- Kholdstare --------------------------------------------------------------------
 * The Ice Palace's boss: a cloud with an eye, sealed in a shell of ice that drifts
 * about the room with it. Only fire breaks the shell (the Fire Rod, or Bombos); when
 * it melts, the eye flickers and splits in three. Each drifts on its own, speeding up
 * and slowing down, bouncing off the walls. All the while ice falls from the
 * ceiling onto Link and bursts.
 *
 * Type 0 the eye in the shell, type 1 the three it splits into. Fields: targetX/Y
 * the speed, unused[0] [1] the speed it is heading for (z_vel, z_subpos), head the
 * pupil's look (0 east, 4 south), idx its looks (0-3), coerced 1 while the ice is
 * falling from this one.
 */
enum { KH_INIT, KH_SPEEDUP, KH_SLOW, KH_SPLIT };

#define KHOLD_SHELL_HITS 6

static void KholdstareSpawnIce(AlttpEnemy* this) {
    if (++this->turns & 127) {
        return;
    }
    {
        AlttpEnemy* ice = (AlttpEnemy*)CreateEnemy(ALTTP_BOLT, BT_ICE);
        if (ice != NULL) {
            CopyPosition(&gPlayerEntity.base, &ice->base);
            ice->base.parent = super;
            ice->base.type2 = 0;
        }
    }
}

static void KholdstareTick(AlttpEnemy* this) {
    s8* tx = (s8*)&this->unused[0];
    s8* ty = (s8*)&this->unused[1];
    bool32 shelled = super->type == 0;
    u32 hit;
    if (super->action == KH_INIT) {
        AlttpInit(this, AP_KHOLD, AF_KHOLD_0);
        ((Enemy*)this)->enemyFlags |= EM_FLAG_BOSS;
        super->spritePriority.b0 = 3;
        super->action = KH_SPEEDUP;
        this->step = Random();
        this->turns = 0;
        this->burst = 0;
        if (shelled) {
            u32 i;
            this->shield = 1;
            this->coerced = 1;
            super->spriteSettings.draw = 0;
            super->timer = 64;
            this->targetX = this->targetY = 0;
            *tx = 16;
            *ty = 16;
            for (i = 0; i < 2; i++) {
                Entity* s = SpawnAt(KHOLDSTARE_SHELL, i, super, 0, 0);
                if (s != NULL) {
                    s->collisionLayer = super->collisionLayer;
                }
            }
        } else {
            super->timer = 32;
        }
        {
            Entity* p = SpawnAt(BOSS_PIECE, BP_KPUPIL, super, 0, 0);
            if (p != NULL) {
                p->collisionLayer = super->collisionLayer;
            }
        }
    }
    this->step++;
    if (this->coerced) {
        KholdstareSpawnIce(this);
    }
    // the cloud drifts by in four looks; the pupil follows Link
    if ((s8)--this->rangeX < 0) {
        this->rangeX = 10;
        this->idx = (this->idx + 1) & 3;
    }
    if (!(this->step & 3)) {
        this->head = AngleToHeading(AngleTo(gPlayerEntity.base.x.HALF.HI - super->x.HALF.HI,
                                            gPlayerEntity.base.y.HALF.HI - super->y.HALF.HI));
    }
    if (shelled) {
        hit = MoveVelWalls2(super, this->targetX, this->targetY, 40, 32);
    } else {
        hit = MoveVelWalls(super, this->targetX, this->targetY, 12);
    }
    switch (super->action) {
        case KH_SPEEDUP:
            if (super->timer == 0) {
                super->action = KH_SLOW;
                super->timer = (Random() & 63) + 32;
                break;
            }
            super->timer--;
            if (this->targetX != *tx) {
                this->targetX += this->targetX < *tx ? 1 : -1;
            }
            if (this->targetY != *ty) {
                this->targetY += this->targetY < *ty ? 1 : -1;
            }
            goto bounce;
        case KH_SLOW:
            if (super->timer == 0) {
                u32 r = Random();
                super->action = KH_SPEEDUP;
                super->timer = (Random() & 63) + 96;
                if (!(r & 0x1c)) {
                    s16 ax, ay;
                    AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI, 24, &ax, &ay);
                    *tx = ax;
                    *ty = ay;
                } else {
                    static const s8 kx[4] = { 16, 16, -16, -16 };
                    static const s8 ky[4] = { -16, 16, 16, -16 };
                    *tx = kx[r & 3];
                    *ty = ky[r & 3];
                }
                break;
            }
            super->timer--;
            ApproachVel(&this->targetX, 0);
            ApproachVel(&this->targetY, 0);
        bounce:
            if (hit & 1) {
                this->targetX = -this->targetX;
                *tx = -*tx;
            }
            if (hit & 2) {
                this->targetY = -this->targetY;
                *ty = -*ty;
            }
            break;
        case KH_SPLIT:
            // bare, it flickers, then splits in three
            this->targetX = this->targetY = 0;
            super->spriteSettings.draw = (this->step >> 1) & 1;
            if (--super->timer == 0) {
                static const s8 sx[3] = { 32, -32, 0 };
                static const s8 sy[3] = { -32, -32, 48 };
                u32 i;
                Entity* first = NULL;
                for (i = 0; i < 3; i++) {
                    AlttpEnemy* e = (AlttpEnemy*)CreateEnemy(KHOLDSTARE, 1);
                    if (e != NULL) {
                        CopyPosition(super, &e->base);
                        e->unused[0] = sx[i];
                        e->unused[1] = sy[i];
                        e->coerced = first == NULL;
                        e->turns = this->turns;
                        if (first == NULL) {
                            first = &e->base;
                        }
                    }
                }
                EnqueueSFX(SFX_BOSS_DIE);
                DeleteChildren(super);
                DeleteThisEntity();
            }
            break;
    }
    if (shelled && super->action != KH_SPLIT) {
        super->spriteSettings.draw = 0; // inside the ice
    }
    SetFrame(super, AF_KHOLD_0 + this->idx);
    AlttpEndTick(this);
}

static void KholdstareOnCollision(AlttpEnemy* this);
static void KholdstareOnDeath(AlttpEnemy* this) {
    if (this->burst == 0) {
        Entity* other;
        // another eye takes over the falling ice
        if (this->coerced) {
            u32 i;
            for (i = 0; i < ARRAY_COUNT(gEntityLists); i++) {
                LinkedList* list = &gEntityLists[i];
                for (other = list->first; (u32)other != (u32)list; other = other->next) {
                    if (other != super && other->kind == ENEMY && other->id == KHOLDSTARE && other->health) {
                        ((AlttpEnemy*)other)->coerced = 1;
                        i = ARRAY_COUNT(gEntityLists);
                        break;
                    }
                }
            }
        }
        DeleteChildren(super);
    }
    if (BossBurst(this, 8, 8)) {
        DeleteThisEntity();
    }
}

static void (*const Kholdstare_Functions[])(AlttpEnemy*) = {
    KholdstareTick, KholdstareOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    KholdstareOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void KholdstareOnCollision(AlttpEnemy* this) {
    super->knockbackDuration = 0;
    if (this->shield) {
        TookHit(this);
    } else if (super->health < this->savedHealth) {
        EnqueueSFX(SFX_BOSS_HIT);
    }
    EnemyFunctionHandlerAfterCollision(super, (void (*const*)())Kholdstare_Functions);
}

void Kholdstare(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Kholdstare_Functions);
    BossCam(super, -16);
}

/** Kholdstare's shell of ice, in two halves (type 0 the left, which takes the hits;
 * type 1 the right). Fire cracks it; at six blows it slumps and melts away. Fields:
 * turns the blows of fire, pose 0-2 its cracks, then 3-5 melting. */
static void KholdShellTick(AlttpEnemy* this) {
    AlttpEnemy* eye = (AlttpEnemy*)super->parent;
    bool32 left = super->type == 0;
    if (super->action == 0) {
        AlttpInit(this, AP_KSHELL, left ? AF_KSHELL_L_0 : AF_KSHELL_R_0);
        ((Enemy*)this)->enemyFlags |= EM_FLAG_BOSS;
        super->spritePriority.b0 = 2;
        super->spriteSettings.shadow = 0;
        this->pose = 0;
        this->turns = 0;
        if (!left) {
            COLLISION_OFF(super);
            super->health = 0;
        }
        this->savedHealth = super->health;
    }
    if (!ParentAlive(super)) {
        DeleteThisEntity();
    }
    if (this->cooldown) {
        this->cooldown--;
    }
    super->x.HALF.HI = eye->base.x.HALF.HI;
    super->y.HALF.HI = eye->base.y.HALF.HI;
    if (!left) {
        // the right half copies the left one's state
        AlttpEnemy* l = (AlttpEnemy*)super->child;
        if (l == NULL) {
            u32 i;
            Entity* e;
            for (i = 0; i < ARRAY_COUNT(gEntityLists) && l == NULL; i++) {
                LinkedList* list = &gEntityLists[i];
                for (e = list->first; (u32)e != (u32)list; e = e->next) {
                    if (e->kind == ENEMY && e->id == KHOLDSTARE_SHELL && e->type == 0 && e->parent == super->parent) {
                        l = (AlttpEnemy*)e;
                        break;
                    }
                }
            }
            super->child = &l->base;
        }
        if (l == NULL || l->base.next == NULL || l->base.id != KHOLDSTARE_SHELL) {
            DeleteThisEntity();
        }
        this->pose = l->pose;
        super->spriteSettings.draw = l->base.spriteSettings.draw;
    } else {
        if (this->pose >= 3) {
            // melting away
            if (++this->step >= 8) {
                this->step = 0;
                if (++this->pose == 6) {
                    eye->base.action = KH_SPLIT;
                    eye->base.timer = 128;
                    eye->shield = 1;
                    eye->targetX = eye->targetY = 0;
                    super->spriteSettings.draw = 0;
                    DeleteThisEntity();
                }
            }
        } else if (LinkNear(super, 36, 28) && gPlayerEntity.base.knockbackDuration == 0) {
            // solid: it shoves Link back
            BounceLink(super, 8);
        }
        super->health = 255;
        this->savedHealth = 255;
    }
    if (this->pose < 3) {
        SetFrame(super, (left ? AF_KSHELL_L_0 : AF_KSHELL_R_0) + this->pose);
    } else {
        SetFrame(super, (left ? AF_KSHELL_L_MELT_1 : AF_KSHELL_R_MELT_1) + this->pose - 3);
    }
}

static void KholdShellOnCollision(AlttpEnemy* this);
static void KholdShellOnDeath(AlttpEnemy* this) {
    super->health = 255;
}

static void (*const KholdShell_Functions[])(AlttpEnemy*) = {
    KholdShellTick, KholdShellOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    KholdShellOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void KholdShellOnCollision(AlttpEnemy* this) {
    Entity* by = super->contactedEntity;
    super->knockbackDuration = 0;
    super->health = 255;
    if (this->pose < 3) {
        if (IsFire(super, by)) {
            if (this->cooldown == 0) {
                this->cooldown = 16;
                this->turns += (by->kind == OBJECT) ? KHOLD_SHELL_HITS : 1; // Bombos melts it at once
                EnqueueSFX(SFX_ICE_BLOCK_MELT);
                CreateFx(super, FX_FIRE, 0);
                InitScreenShake(8, 0);
                if (this->turns >= KHOLD_SHELL_HITS) {
                    this->pose = 3;
                    this->step = 0;
                    COLLISION_OFF(super);
                    EnqueueSFX(SFX_EVAPORATE);
                } else {
                    this->pose = this->turns * 3 / KHOLD_SHELL_HITS;
                }
            }
        } else if (by != NULL && by->kind == PLAYER_ITEM) {
            EnqueueSFX(SFX_METAL_CLINK);
        }
    }
    EnemyFunctionHandlerAfterCollision(super, (void (*const*)())KholdShell_Functions);
}

void KholdstareShell(AlttpEnemy* this) {
    if (super->type == 0) {
        EnemyFunctionHandler(super, (EntityActionArray)KholdShell_Functions);
    } else {
        KholdShellTick(this);
    }
}

/* ---- Vitreous ------------------------------------------------------------------------
 * Misery Mire's boss: a great eye sunk in a mass of small ones. Now and then a small
 * eye leaves the heap and flies at where Link stood, then back; while any are left
 * the great eye sleeps, waking every so often to spew lightning. With the small eyes
 * gone it heaves itself out and hops after Link.
 *
 * Fields: turns the small eyes still about (sprite_G), unused[0] the sleep's countdown
 * (sprite_A), pose its look, homeX/Y its height and fall (1/16 pixel).
 */
enum { VI_INIT, VI_DORMANT, VI_SPEW, VI_PURSUE };

static void VitreousLook(AlttpEnemy* this, u32 t) {
    static const u8 look[2] = { 2, 1 };
    s32 dx = gPlayerEntity.base.x.HALF.HI - super->x.HALF.HI;
    if (t == 0x40 || t == 0x42) {
        // Sprite_SpawnLightning
        static const s8 lx[8] = { -8, 8, 8, -8, 8, -8, -8, 8 };
        u32 i = Random() & 7;
        AlttpEnemy* b = (AlttpEnemy*)SpawnBolt(super, BT_LIGHTNING, lx[i], 12, 0, 0);
        if (b != NULL) {
            b->idx = i;
            b->base.collisionLayer = super->collisionLayer;
            EnqueueSFX(SFX_ITEM_SWORD_BEAM);
        }
    }
    this->pose = Abs(dx) >= 16 ? look[dx < 0] : 0;
}

static void VitreousTick(AlttpEnemy* this) {
    static const u8 sleepFor[10] = { 0x20, 0x20, 0x20, 0x40, 0x60, 0x80, 0xa0, 0xc0, 0xe0, 0 };
    s16* z = &this->homeX;
    s16* vz = &this->homeY;
    if (super->action == VI_INIT) {
        static const s8 ex[13] = { 8, 22, -8, -22, 0, 14, 19, 33, 26, -14, -19, -33, -26 };
        static const s8 ey[13] = { -8, -12, -8, -12, 0, -20, -1, -12, -24, -20, -1, -12, -24 };
        u32 i;
        AlttpInit(this, AP_VITREOUS, AF_VITREOUS_4);
        BossStartDown(super, 16);
        ((Enemy*)this)->enemyFlags |= EM_FLAG_BOSS;
        super->action = VI_DORMANT;
        super->spritePriority.b0 = 3;
        this->shield = 1;
        this->turns = 0;
        this->unused[0] = 0x80;
        *z = *vz = 0;
        // the small eyes heaped in front of it; the four lowest stay shut (and can't be
        // hurt) until the others are gone
        for (i = 0; i < 13; i++) {
            AlttpEnemy* e = (AlttpEnemy*)SpawnAt(VITREOUS_EYE, i < 4, super, ex[i], ey[i] + 32);
            if (e != NULL) {
                e->homeX = e->base.x.HALF.HI;
                e->homeY = e->base.y.HALF.HI;
                e->step = i * 8 + Random();
                e->base.collisionLayer = super->collisionLayer;
                this->turns++;
            }
        }
    }
    this->step++;
    if (this->cooldown) {
        this->cooldown--;
    }
    // Vitreous_SetMinionsForth: an eye goes for Link about once a second
    if (this->turns && !(this->step & 63)) {
        u32 n = Random() & 15, i, open = 0;
        Entity* e;
        Entity* shut = NULL;
        for (i = 0; i < ARRAY_COUNT(gEntityLists); i++) {
            LinkedList* list = &gEntityLists[i];
            for (e = list->first; (u32)e != (u32)list; e = e->next) {
                if (e->kind == ENEMY && e->id == VITREOUS_EYE && e->parent == super) {
                    if (e->type == 0) {
                        open++;
                    } else {
                        shut = e;
                    }
                }
            }
        }
        if (open == 0 && shut != NULL) {
            // the open eyes are all gone: a shut one opens
            shut->type = 0;
            ((AlttpEnemy*)shut)->shield = 0;
            open = 1;
        }
        while (open && n >= open) {
            n -= open;
        }
        for (i = 0; open && i < ARRAY_COUNT(gEntityLists); i++) {
            LinkedList* list = &gEntityLists[i];
            for (e = list->first; (u32)e != (u32)list; e = e->next) {
                if (e->kind == ENEMY && e->id == VITREOUS_EYE && e->parent == super && e->type == 0 && n-- == 0) {
                    if (e->action == 1) {
                        e->action = 2;
                        EnqueueSFX(SFX_14C);
                    }
                    i = ARRAY_COUNT(gEntityLists);
                    break;
                }
            }
        }
    }
    switch (super->action) {
        case VI_DORMANT:
            this->pose = (this->step & 0x30) ? 4 : 5;
            if (!(this->step & 1) && --this->unused[0] == 0) {
                this->cooldown = 16;
                super->action = VI_SPEW;
                super->timer = 128;
                if (this->turns == 0) {
                    super->action = VI_PURSUE;
                    super->timer = 64;
                    this->shield = 0;
                    EnqueueSFX(SFX_BOSS_HIT);
                }
            }
            break;
        case VI_SPEW:
            if (--super->timer == 0) {
                this->cooldown = 16;
                super->action = VI_DORMANT;
                this->unused[0] = sleepFor[this->turns > 9 ? 9 : this->turns];
                if (this->unused[0] == 0) {
                    this->unused[0] = 1;
                }
            } else {
                VitreousLook(this, super->timer);
            }
            break;
        case VI_PURSUE:
            VitreousLook(this, 0x8b);
            if (super->timer) {
                // shaking itself loose
                super->timer--;
                MoveVel(super, super->timer & 2 ? -8 : 8, 0);
            } else if (this->rangeX) {
                // a moment on the ground between hops (when the sword can reach it)
                this->rangeX--;
            } else {
                if (*vz == 0 && *z == 0) {
                    *vz = 32;
                    AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI, 16, &this->targetX,
                           &this->targetY);
                    EnqueueSFX(SFX_PLY_JUMP);
                }
                if (MoveZ16(z, vz, 2, -64)) {
                    this->rangeX = 24;
                }
                MoveVelWalls(super, this->targetX, this->targetY, 14);
            }
            break;
    }
    SetHeight(super, *z >> 4);
    if (this->cooldown) {
        SetFrame(super, AF_VITREOUS_3);
    } else if (super->action == VI_PURSUE && super->timer == 0) {
        SetFrame(super, *z == 0 ? AF_VITREOUS_LAND : *vz > 0 ? AF_VITREOUS_HOP : AF_VITREOUS_0 + this->pose);
    } else {
        SetFrame(super, AF_VITREOUS_0 + this->pose);
    }
    AlttpEndTick(this);
}

static void VitreousOnCollision(AlttpEnemy* this);
static void VitreousOnDeath(AlttpEnemy* this) {
    if (this->burst == 0) {
        DeleteChildren(super);
    }
    if (BossBurst(this, 10, 10)) {
        DeleteThisEntity();
    }
}

static void (*const Vitreous_Functions[])(AlttpEnemy*) = {
    VitreousTick, VitreousOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    VitreousOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void VitreousOnCollision(AlttpEnemy* this) {
    super->knockbackDuration = 0;
    AlttpOnCollision(this, (void (*const*)())Vitreous_Functions);
}

void Vitreous(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Vitreous_Functions);
    BossCam(super, -8);
}

/** One of Vitreous's small eyes (type 1: one of the four that stay shut, until Vitreous
 * opens it when the open ones are gone). It waits in
 * the heap (homeX/Y), and when sent flies at where Link was (targetX/Y), then home. */
enum { VE_INIT, VE_WAIT, VE_GO, VE_BACK };

static void VitreousEyeTick(AlttpEnemy* this) {
    static const s8 wx[4] = { 1, 0, -1, 0 };
    static const s8 wy[4] = { 0, 1, 0, -1 };
    s16 vx, vy;
    if (super->action == VE_INIT) {
        AlttpInit(this, AP_VITREOUS, super->type ? AF_VEYE_1 : AF_VEYE_0);
        super->action = VE_WAIT;
        super->spritePriority.b0 = 2;
        super->spriteSettings.shadow = 0;
        if (super->type) {
            this->shield = 1;
        }
    }
    if (!ParentAlive(super)) {
        DeleteThisEntity();
    }
    this->step++;
    super->spriteOffsetX = wx[(this->step >> 4) & 3];
    super->spriteOffsetY = wy[(this->step >> 4) & 3];
    switch (super->action) {
        case VE_WAIT:
            this->targetX = gPlayerEntity.base.x.HALF.HI;
            this->targetY = gPlayerEntity.base.y.HALF.HI;
            break;
        case VE_GO:
            AimVel(super, this->targetX, this->targetY, 16, &vx, &vy);
            MoveVel(super, vx, vy);
            if (Abs(super->x.HALF.HI - this->targetX) < 4 && Abs(super->y.HALF.HI - this->targetY) < 4) {
                super->action = VE_BACK;
            }
            break;
        case VE_BACK:
            AimVel(super, this->homeX, this->homeY, 16, &vx, &vy);
            MoveVel(super, vx, vy);
            if (Abs(super->x.HALF.HI - this->homeX) < 4 && Abs(super->y.HALF.HI - this->homeY) < 4) {
                super->x.HALF.HI = this->homeX;
                super->y.HALF.HI = this->homeY;
                super->action = VE_WAIT;
            }
            break;
    }
    if (super->type) {
        SetFrame(super, AF_VEYE_1);
    } else {
        SetFrame(super, (this->step & 0x7f) < 6 ? AF_VEYE_BLINK : AF_VEYE_0);
    }
    AlttpEndTick(this);
}

static void VitreousEyeOnCollision(AlttpEnemy* this);
static void VitreousEyeOnDeath(AlttpEnemy* this) {
    AlttpEnemy* v = (AlttpEnemy*)super->parent;
    if (ParentAlive(super) && v->turns) {
        v->turns--;
    }
    CreateFx(super, FX_DEATH, 0);
    DeleteThisEntity();
}

static void (*const VitreousEye_Functions[])(AlttpEnemy*) = {
    VitreousEyeTick, VitreousEyeOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    VitreousEyeOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void VitreousEyeOnCollision(AlttpEnemy* this) {
    super->knockbackDuration = 0;
    AlttpOnCollision(this, (void (*const*)())VitreousEye_Functions);
}

void VitreousEye(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)VitreousEye_Functions);
}

/* ---- Trinexx -----------------------------------------------------------------------
 * Turtle Rock's boss: a stone turtle with three heads. The rock head and its shell
 * plod between four places, and every other stop it lunges its head out at Link. The
 * side heads sway on their long necks and breathe at Link: the red one fire, the blue
 * one ice. Only the opposite cold or heat gets through to them: the Ice Rod for the
 * fire head, the Fire Rod for the ice head; stunned by it, they can be cut. With both
 * gone the shell bursts, and the rock head slithers out as a snake whose body has one
 * soft, flashing scale.
 */
#define TRINEXX_HIST 64
#define TRINEXX_SEGS 6
#define TRINEXX_NECK 4
#define TRINEXX_WEAK_HITS 8

typedef struct {
    u8 angle[2][9];  /**< each side head's neck, head first (alt_sprite_type) */
    u8 radius[2][9]; /**< (alt_sprite_y_hi) */
    u8 jaw[2];
    u8 heads;        /**< side heads alive */
    u8 alive[2];
    u8 stunned[2];
    u8 final;        /**< the snake */
    u8 lunging;
    u8 legStep;      /**< sprite_subtype2: the legs' step */
    u8 hist;
    u8 weakHits;
    s16 bodyX, bodyY; /**< the shell's middle */
    s16 headX, headY; /**< the rock head */
    s16 histX[TRINEXX_HIST], histY[TRINEXX_HIST];
} TrinexxState;
#define gTrinexx (*(TrinexxState*)(gAlttpEnemyState + 0xc00))

/* The rock head sits this far in front of the shell's middle. */
#define TRINEXX_HEAD_DY 28

enum { TX_INIT, TX_WAIT, TX_WALK, TX_WINDUP, TX_LUNGE, TX_BURST, TX_SNAKE, TX_HUNT };

static void TrinexxTick(AlttpEnemy* this) {
    static const u8 tabX[4] = { 0x60, 0x78, 0x78, 0x90 };
    static const u8 tabY[4] = { 0x80, 0x70, 0x60, 0x80 };
    static const s8 snakeVx[4] = { 0, -31, 0, 31 };
    static const s8 snakeVy[4] = { 31, 0, -31, 0 };
    static const u8 snakeGfx[8] = { 4, 5, 6, 7, 0, 1, 2, 3 };
    s16* vx = &this->targetX;
    s16* vy = &this->targetY;
    u32 i;
    if (super->action == TX_INIT) {
        AlttpInit(this, AP_TRINEXX, AF_TRINEXX_0);
        BossStartDown(super, 40);
        ((Enemy*)this)->enemyFlags |= EM_FLAG_BOSS;
        MemClear(&gTrinexx, sizeof(gTrinexx));
        super->action = TX_WAIT;
        super->timer = 128;
        super->spritePriority.b0 = 2;
        super->spriteSettings.shadow = 0;
        this->shield = 1;
        this->homeX = super->x.HALF.HI;
        this->homeY = super->y.HALF.HI;
        this->turns = 0;
        this->idx = 0;
        this->pose = 0; // (the room's spawn data leaves bytes here)
        *vx = *vy = 0;
        gTrinexx.headX = super->x.HALF.HI;
        gTrinexx.headY = super->y.HALF.HI;
        gTrinexx.bodyX = super->x.HALF.HI;
        gTrinexx.bodyY = super->y.HALF.HI - TRINEXX_HEAD_DY;
        gTrinexx.heads = 2;
        for (i = 0; i < 2; i++) {
            u32 n;
            Entity* e;
            gTrinexx.alive[i] = 1;
            for (n = 0; n < 9; n++) {
                gTrinexx.angle[i][n] = 0x40;
            }
            e = SpawnAt(TRINEXX_HEAD, i, super, 0, 0);
            if (e != NULL) {
                e->collisionLayer = super->collisionLayer;
            }
            e = SpawnAt(BOSS_PIECE, BP_TSHELL, super, 0, 0);
            if (e != NULL) {
                e->type2 = i;
                e->collisionLayer = super->collisionLayer;
            }
            for (n = 0; n < TRINEXX_NECK; n++) {
                e = SpawnAt(BOSS_PIECE, BP_TNECK, super, 0, 0);
                if (e != NULL) {
                    e->type2 = i * 8 + n;
                    e->collisionLayer = super->collisionLayer;
                }
            }
        }
        {
            Entity* e = SpawnAt(BOSS_PIECE, BP_TLEGS, super, 0, 0);
            if (e != NULL) {
                e->collisionLayer = super->collisionLayer;
            }
            // the neck that shows as it lunges
            for (i = 0; i < 3; i++) {
                e = SpawnAt(BOSS_PIECE, BP_TSEG, super, 0, 0);
                if (e != NULL) {
                    e->type2 = 16 + i;
                    e->collisionLayer = super->collisionLayer;
                }
            }
        }
    }
    this->step++;
    if (gTrinexx.final) {
        // the snake: straight runs, turning at walls, then a while hunting Link
        u32 a;
        gTrinexx.hist = (gTrinexx.hist + 1) & (TRINEXX_HIST - 1);
        gTrinexx.histX[gTrinexx.hist] = super->x.HALF.HI;
        gTrinexx.histY[gTrinexx.hist] = super->y.HALF.HI;
        if (!(this->step & 7)) {
            EnqueueSFX(SFX_14C);
        }
        if (super->action == TX_SNAKE) {
            if (--super->timer == 0) {
                super->action = TX_HUNT;
                super->timer = 192;
            }
            *vx = snakeVx[this->face];
            *vy = snakeVy[this->face];
            if (MoveVelWalls(super, *vx, *vy, 10)) {
                this->face = (this->face + 1) & 3;
                this->cooldown = 8;
            }
        } else {
            if (!(this->step & 1)) {
                s16 ax, ay;
                AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI, 31, &ax, &ay);
                ApproachVel(vx, ax);
                ApproachVel(vy, ay);
            }
            if (MoveVelWalls(super, *vx, *vy, 10) || --super->timer == 0) {
                super->action = TX_SNAKE;
                super->timer = 128;
            }
        }
        if (this->cooldown) {
            this->cooldown--;
        }
        a = AngleTo(*vx, *vy);
        SetFrame(super, AF_TRINEXX_0 + snakeGfx[((a + 16) >> 5) & 7]);
        if (gTrinexx.weakHits >= TRINEXX_WEAK_HITS) {
            super->health = 0;
        }
        AlttpEndTick(this);
        return;
    }
    if (gTrinexx.heads == 0 && super->action != TX_BURST) {
        // both side heads gone: the shell bursts
        super->action = TX_BURST;
        super->timer = 255;
        this->turns = 0;
        gTrinexx.lunging = 0;
        super->x.HALF.HI = gTrinexx.bodyX;
        super->y.HALF.HI = gTrinexx.bodyY + TRINEXX_HEAD_DY;
        EnqueueSFX(SFX_BOSS_DIE);
    }
    switch (super->action) {
        case TX_WAIT:
            if (super->timer) {
                super->timer--;
                break;
            }
            // the side heads keep it still while they breathe
            if (gTrinexx.jaw[0] || gTrinexx.jaw[1]) {
                break;
            }
            {
                u32 j = Random() & 3;
                if (j == this->idx) {
                    break;
                }
                if (++this->turns == 2) {
                    this->turns = 0;
                    super->action = TX_WINDUP;
                    super->timer = 80;
                    break;
                }
                this->idx = j;
                this->unused[0] = (Random() & 3) == 0; // fast
                super->action = TX_WALK;
            }
            break;
        case TX_WALK: {
            s32 tx = this->homeX + tabX[this->idx] - 0x78;
            s32 ty = this->homeY + tabY[this->idx] - 0x70;
            u32 n = this->unused[0] ? 2 : 1;
            AimVel(super, tx, ty, this->unused[0] ? 16 : 8, vx, vy);
            MoveVel(super, *vx, *vy);
            if (Abs(super->x.HALF.HI - tx) < 2 && Abs(super->y.HALF.HI - ty) < 2) {
                super->action = TX_WAIT;
                super->timer = 48;
            }
            do {
                gTrinexx.legStep += *vx < 0 ? 1 : -1;
                if (!(gTrinexx.legStep & 0xf)) {
                    EnqueueSFX(SFX_THUD_HEAVY);
                }
            } while (--n);
            gTrinexx.bodyX = super->x.HALF.HI;
            gTrinexx.bodyY = super->y.HALF.HI - TRINEXX_HEAD_DY;
            break;
        }
        case TX_WINDUP:
            if (--super->timer == 0) {
                super->action = TX_LUNGE;
                super->timer = 64;
                AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI, 48, vx, vy);
                gTrinexx.lunging = 1;
                EnqueueSFX(SFX_14C);
            }
            break;
        case TX_LUNGE:
            MoveVel(super, *vx, *vy);
            if (--super->timer == 0) {
                super->x.HALF.HI = gTrinexx.bodyX;
                super->y.HALF.HI = gTrinexx.bodyY + TRINEXX_HEAD_DY;
                super->action = TX_WAIT;
                super->timer = 48;
                gTrinexx.lunging = 0;
            } else if (super->timer == 0x20) {
                *vx = -*vx;
                *vy = -*vy;
            }
            break;
        case TX_BURST:
            if (super->timer == 0 || --super->timer == 0) {
                // out of the shell: the snake
                Entity* e;
                gTrinexx.final = 1;
                for (i = 0; i < ARRAY_COUNT(gEntityLists); i++) {
                    LinkedList* list = &gEntityLists[i];
                    Entity* next;
                    for (e = list->first; (u32)e != (u32)list; e = next) {
                        next = e->next;
                        if (e->kind == ENEMY && e->parent == super && e->id == BOSS_PIECE) {
                            DeleteEntity(e);
                        }
                    }
                }
                for (i = 0; i < TRINEXX_HIST; i++) {
                    gTrinexx.histX[i] = super->x.HALF.HI;
                    gTrinexx.histY[i] = super->y.HALF.HI;
                }
                for (i = 0; i < TRINEXX_SEGS; i++) {
                    e = SpawnAt(BOSS_PIECE, BP_TSEG, super, 0, 0);
                    if (e != NULL) {
                        e->type2 = i;
                        e->collisionLayer = super->collisionLayer;
                    }
                }
                super->action = TX_SNAKE;
                super->timer = 128;
                this->face = 0;
                this->shield = 1;
                super->spritePriority.b0 = 1;
                *vx = *vy = 0;
            } else if (super->timer >= 0xe0) {
                // the shell heaves up
                gTrinexx.bodyY -= (super->timer & 1);
                InitScreenShake(2, 0);
            } else if (!(super->timer & 3)) {
                Entity* fx = CreateFx(super, FX_GIANT_EXPLOSION4, 0);
                if (fx != NULL) {
                    fx->x.HALF.HI = gTrinexx.bodyX + (s32)(Random() & 63) - 32;
                    fx->y.HALF.HI = gTrinexx.bodyY + (s32)(Random() & 47) - 24;
                }
                EnqueueSFX(SFX_ITEM_BOMB_EXPLODE);
            }
            break;
    }
    gTrinexx.headX = super->x.HALF.HI;
    gTrinexx.headY = super->y.HALF.HI;
    if (!(this->step & 63)) {
        s32 dx = gPlayerEntity.base.x.HALF.HI - super->x.HALF.HI;
        this->pose = Abs(dx) < 24 ? 0 : dx < 0 ? 1 : 7;
    }
    SetFrame(super, AF_TRINEXX_0 + this->pose);
    AlttpEndTick(this);
}

static void TrinexxOnCollision(AlttpEnemy* this);
static void TrinexxOnDeath(AlttpEnemy* this) {
    if (this->burst == 0) {
        DeleteChildren(super);
    }
    if (BossBurst(this, 8, 8)) {
        DeleteThisEntity();
    }
}

static void (*const Trinexx_Functions[])(AlttpEnemy*) = {
    TrinexxTick, TrinexxOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    TrinexxOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void TrinexxOnCollision(AlttpEnemy* this) {
    super->knockbackDuration = 0;
    if (super->health != 0) {
        if (TookHit(this)) {
            EnqueueSFX(SFX_METAL_CLINK);
        }
    }
    EnemyFunctionHandlerAfterCollision(super, (void (*const*)())Trinexx_Functions);
}

void Trinexx(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Trinexx_Functions);
    if (gTrinexx.final) {
        BossCam(super, -8);
    } else {
        // the shell, with the side heads above it
        BossCamAt(gTrinexx.bodyX, gTrinexx.bodyY - 16);
    }
}

/** A side head (type 0 the fire head, on the left; 1 the ice head). Fields: face its
 * pose (sprite_D, 0-4), pose the jaw, cooldown frames left stunned. */
enum { TH_INIT, TH_WAIT, TH_SWAY, TH_IDLE, TH_BREATHE };

static const u8 sNeckAngle[45] = {
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x58, 0x64, 0x6a, 0x6f, 0x74, 0x7a, 0x7e,
    0x80, 0x80, 0x39, 0x48, 0x52, 0x5c, 0x65, 0x73, 0x77, 0x7a, 0x80, 0x1e, 0x24, 0x29, 0x2e, 0x34,
    0x3a, 0x44, 0x4d, 0x80, 0x0a, 0x11, 0x17, 0x1c, 0x22, 0x2a, 0x36, 0x3a, 0x80,
};
static const u8 sNeckRadius[45] = {
    0x30, 0x28, 0x23, 0x1e, 0x19, 0x13, 0x0c, 0x06, 0x00, 0x2f, 0x26, 0x21, 0x1d, 0x18, 0x12, 0x0c,
    0x06, 0x00, 0x2f, 0x27, 0x22, 0x1d, 0x18, 0x12, 0x0c, 0x06, 0x00, 0x2f, 0x27, 0x22, 0x1d, 0x18,
    0x12, 0x0c, 0x06, 0x00, 0x48, 0x3a, 0x32, 0x29, 0x22, 0x19, 0x10, 0x07, 0x00,
};

/** Where a part of a side head's neck is (n 0 the head, 8 the shell). */
static void TrinexxNeckAt(u32 head, u32 n, s32* x, s32* y) {
    u32 a = gTrinexx.angle[head][n];
    s32 r = gTrinexx.radius[head][n];
    if (head == 0) {
        a = 0x200 - a; // the left neck mirrors the right
    }
    *x = gTrinexx.bodyX + (head ? 13 : -14) + HalfSin(a, r);
    *y = gTrinexx.bodyY - 16 + HalfSin(a + 0x80, r);
}

static void TrinexxHeadTick(AlttpEnemy* this) {
    u32 k = super->type;
    s32 x, y;
    if (super->action == TH_INIT) {
        AlttpInit(this, k ? AP_TICE : AP_TFIRE, AF_THEAD_0);
        ((Enemy*)this)->enemyFlags |= EM_FLAG_BOSS;
        super->action = TH_WAIT;
        super->timer = k ? 255 : 128;
        super->spritePriority.b0 = 1;
        super->spriteSettings.shadow = 0;
        this->shield = 1;
        this->face = 0;
        this->pose = 0;
        this->cooldown = 0;
    }
    if (!ParentAlive(super) || gTrinexx.final) {
        DeleteThisEntity();
    }
    this->step++;
    if (this->cooldown) {
        // stunned (frozen or burnt): now the sword cuts it
        this->shield = 0;
        if (--this->cooldown == 0) {
            this->shield = 1;
            SetPalette(super, k ? AP_TICE : AP_TFIRE);
            super->action = TH_IDLE;
            super->timer = 32;
        }
        gTrinexx.stunned[k] = this->cooldown != 0;
        gTrinexx.jaw[k] = 0;
        this->pose = 0;
    } else {
        switch (super->action) {
            case TH_WAIT:
                if (--super->timer == 0) {
                    super->action = TH_SWAY;
                }
                break;
            case TH_IDLE:
                if (super->timer) {
                    super->timer--;
                } else {
                    u32 i = (Random() & 7) + 1;
                    if (i < 5 && this->face != i) {
                        u32 was = this->face;
                        this->face = i;
                        super->action = TH_SWAY;
                        if (was == 1 && (Random() & 1) && !gTrinexx.lunging) {
                            super->action = TH_BREATHE;
                            super->timer = 127;
                        }
                    }
                }
                break;
            case TH_SWAY: {
                // each part of the neck creeps toward the pose
                u32 j = this->face * 9, n, moved = 0;
                for (n = 0; n < 9; n++, j++) {
                    u8* a = &gTrinexx.angle[k][n];
                    u8* r = &gTrinexx.radius[k][n];
                    if (*a != sNeckAngle[j]) {
                        *a += *a < sNeckAngle[j] ? 1 : -1;
                        moved++;
                    }
                    if (*a != sNeckAngle[j]) {
                        *a += *a < sNeckAngle[j] ? 1 : -1;
                        moved++;
                    }
                    if (*r != sNeckRadius[j]) {
                        *r += *r < sNeckRadius[j] ? 1 : -1;
                        moved++;
                    }
                }
                if (moved == 0) {
                    super->action = TH_IDLE;
                    super->timer = Random() & 15;
                }
                break;
            }
            case TH_BREATHE: {
                u32 j = super->timer;
                if (j == 0 || --super->timer == 0) {
                    super->action = TH_IDLE;
                    super->timer = 32;
                    this->pose = 0;
                    break;
                }
                if (j == 64) {
                    // Sidenexx_ExhaleDanger
                    if (k) {
                        SpawnBolt(super, BT_TICE, 0, 8, -2, 0);
                        SpawnBolt(super, BT_TICE, 0, 8, 1, 0);
                        EnqueueSFX(SFX_ICE_BLOCK_SLIDE);
                    } else {
                        SpawnBolt(super, BT_TFIRE, 0, 8, 0, 0);
                        EnqueueSFX(SFX_ITEM_LANTERN_ON);
                    }
                }
                this->pose = j < 8 ? j : j < 121 ? 8 : 127 - j;
                if (j >= 64 && !(this->step & 7)) {
                    CreateFx(super, k ? FX_BLUE_SPARKLE : FX_SPARKLE2, 0);
                }
                break;
            }
        }
        gTrinexx.jaw[k] = super->action == TH_BREATHE;
        gTrinexx.stunned[k] = 0;
    }
    TrinexxNeckAt(k, 0, &x, &y);
    super->x.HALF.HI = x;
    super->y.HALF.HI = y;
    super->spriteSettings.draw = this->cooldown ? (this->step & 2) != 0 || this->cooldown > 32 : 1;
    SetFrame(super, AF_THEAD_0 + (this->pose >> 1));
    AlttpEndTick(this);
}

static void TrinexxHeadOnCollision(AlttpEnemy* this);
static void TrinexxHeadOnDeath(AlttpEnemy* this) {
    if (this->burst == 0) {
        gTrinexx.alive[super->type] = 0;
        gTrinexx.jaw[super->type] = 0;
        if (gTrinexx.heads) {
            gTrinexx.heads--;
        }
        COLLISION_OFF(super);
    }
    // Sidenexx_Explode: a blast down the neck
    if (++this->burst >= 48) {
        DeleteThisEntity();
    }
    if ((this->burst & 7) == 0) {
        s32 x, y;
        Entity* fx = CreateFx(super, FX_GIANT_EXPLOSION4, 0);
        TrinexxNeckAt(super->type, (this->burst >> 3) + 1 < 9 ? (this->burst >> 3) + 1 : 8, &x, &y);
        if (fx != NULL) {
            fx->x.HALF.HI = x;
            fx->y.HALF.HI = y;
        }
        EnqueueSFX(SFX_ITEM_BOMB_EXPLODE);
    }
    super->spriteSettings.draw = this->burst < 16;
}

static void (*const TrinexxHead_Functions[])(AlttpEnemy*) = {
    TrinexxHeadTick, TrinexxHeadOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    TrinexxHeadOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void TrinexxHeadOnCollision(AlttpEnemy* this) {
    Entity* by = super->contactedEntity;
    u32 k = super->type;
    super->knockbackDuration = 0;
    if (IsRodShot(by, k ? ITEM_FIRE_ROD : ITEM_ICE_ROD)) {
        // the one thing that gets through: it freezes (or burns) and hangs stunned
        if (this->cooldown == 0) {
            EnqueueSFX(k ? SFX_ITEM_LANTERN_ON : SFX_ICE_BLOCK_SLIDE);
            EnqueueSFX(SFX_BOSS_HIT);
            SetPalette(super, k ? AP_TFIRE : AP_ICEBALL);
        }
        this->cooldown = 128;
        this->shield = 0;
        super->action = TH_IDLE;
    } else if (this->shield) {
        if (TookHit(this)) {
            EnqueueSFX(SFX_METAL_CLINK);
        }
    } else if (super->health < this->savedHealth) {
        EnqueueSFX(SFX_BOSS_HIT);
    }
    EnemyFunctionHandlerAfterCollision(super, (void (*const*)())TrinexxHead_Functions);
}

void TrinexxHead(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)TrinexxHead_Functions);
}

/** The bosses' pieces: Blind's head (on his body, or flown off), Kholdstare's pupil,
 * Trinexx's legs, a half of its shell, a part of a side head's neck, a part of the
 * snake (or of the rock head's neck as it lunges), and a Chain Chomp's chain link. */
/** The pieces Link can strike (Blind's flown head, Kholdstare's pupil, Trinexx's shell,
 * necks and snake): they go through the collision handler. */
static bool32 BossPieceArmored(AlttpEnemy* this) {
    switch (super->type) {
        case BP_FLYHEAD:
        case BP_KPUPIL:
        case BP_TSHELL:
        case BP_TNECK:
        case BP_TSEG:
            return TRUE;
    }
    return FALSE;
}

static void BossPieceTick(AlttpEnemy* this) {
    AlttpEnemy* boss = (AlttpEnemy*)super->parent;
    u32 i = super->type2;
    if (super->action == 0) {
        static const u8 pal[] = { AP_BLINDHEAD, AP_BLINDHEAD, AP_KHOLD, AP_TRINEXX, AP_TSHELL, AP_TRINEXX,
                                  AP_TRINEXX, AP_CHOMPLINK };
        AlttpInit(this, pal[super->type], 0);
        super->spriteSettings.shadow = 0;
        super->spritePriority.b0 = 2;
        this->step = 0;
        switch (super->type) {
            case BP_FLYHEAD:
                super->timer = 48;
                this->idx = 2;
                this->crusher = super->x.HALF.HI >= this->homeX;
                this->turns = 0;
                this->unused[0] = 4;
                this->targetX = this->targetY = 0;
                super->spritePriority.b0 = 1;
                break;
            case BP_KPUPIL:
                super->spritePriority.b0 = 2;
                break;
            case BP_TLEGS:
                super->spritePriority.b0 = 4;
                COLLISION_OFF(super);
                break;
            case BP_TSHELL:
                super->spritePriority.b0 = 3;
                if (i != 0) {
                    COLLISION_OFF(super);
                } else {
                    super->hitbox = (Hitbox*)&gHitbox_TrinexxShell;
                }
                break;
            case BP_TSEG:
                super->spritePriority.b0 = i >= 16 ? 3 : 2;
                this->shield = i != 3;
                break;
            case BP_CHAIN:
                super->spritePriority.b0 = 3;
                COLLISION_OFF(super);
                break;
            case BP_TNECK:
                SetPalette(super, (i >> 3) ? AP_TICE : AP_TFIRE);
                break;
            default:
                COLLISION_OFF(super);
                break;
        }
        if (BossPieceArmored(this)) {
            // armour: Link's sword clinks off it (not a Spark's shock, which would
            // numb the sword), and it hurts Link to touch; the shell is only solid
            super->health = 255;
            super->hitType = super->type == BP_TSHELL ? 81 : 100;
        }
        this->savedHealth = super->health;
    }
    if (!ParentAlive(super) || (boss->base.health == 0 && super->type != BP_CHAIN)) {
        DeleteThisEntity();
    }
    this->step++;
    if (this->cooldown) {
        this->cooldown--;
    }
    if (!BossPieceArmored(this)) {
        super->health = 0;
    }
    super->knockbackDuration = 0;
    super->collisionLayer = boss->base.collisionLayer;
    switch (super->type) {
        case BP_BLINDHEAD: {
            static const s8 dx[15] = BLIND_HEAD_DX;
            static const s8 dy[15] = BLIND_HEAD_DY;
            static const u16 frame[16] = BLIND_HEAD_FRAME;
            u32 g = boss->pose;
            super->x.HALF.HI = boss->base.x.HALF.HI + dx[g];
            super->y.HALF.HI = boss->base.y.HALF.HI + dy[g];
            super->spriteSettings.draw = g < 10 && boss->base.spriteSettings.draw;
            SetFrame(super, frame[boss->head & 15]);
            break;
        }
        case BP_FLYHEAD: {
            // Sprite_Blind_Head: it bounces about, spinning, spitting fire
            static const s8 limX[2] = { 32, -32 };
            static const s8 limY[2] = { 48, -48 };
            static const s8 velX[2] = { 32, -32 };
            static const s8 velY[2] = { 24, -24 };
            static const u16 frame[16] = BLIND_HEAD_FRAME;
            u32 j;
            if (this->idx-- == 0) {
                this->idx = 2;
                this->head = (this->head + 1) & 15;
            }
            SetFrame(super, frame[this->head]);
            if (super->timer) {
                super->timer--;
                break;
            }
            if (!(this->step & 31)) {
                Entity* fb = BlindSpit(super, this->head, 0);
                if (fb != NULL && --this->unused[0] == 0) {
                    this->unused[0] = 4;
                    fb->direction = GetFacingDirection(super, &gPlayerEntity.base);
                }
                if (fb != NULL) {
                    fb->parent = &boss->base;
                }
            }
            j = this->crusher & 1;
            if (this->targetX != velX[j]) {
                this->targetX += j ? -1 : 1;
            }
            if (j == 0 ? super->x.HALF.HI >= this->homeX + limX[0] : super->x.HALF.HI <= this->homeX + limX[1]) {
                this->crusher++;
            }
            j = this->turns & 1;
            if (this->targetY != velY[j]) {
                this->targetY += j ? -1 : 1;
            }
            if (j == 0 ? super->y.HALF.HI >= this->homeY + limY[0] : super->y.HALF.HI <= this->homeY + limY[1]) {
                this->turns++;
            }
            MoveVel(super, this->targetX, this->targetY);
            break;
        }
        case BP_KPUPIL:
            CopyPosition(&boss->base, super);
            super->spriteSettings.draw = boss->base.spriteSettings.draw;
            SetFrame(super, AF_KPUPIL_0 + (boss->head & 15));
            break;
        case BP_TLEGS: {
            static const u8 bob[4] = { 0, 1, 2, 1 };
            super->x.HALF.HI = gTrinexx.bodyX;
            super->y.HALF.HI = gTrinexx.bodyY - 4 + bob[(gTrinexx.legStep >> 3) & 3] - 1;
            SetFrame(super, AF_TLEGS_0 + ((gTrinexx.legStep >> 2) & 3));
            break;
        }
        case BP_TSHELL:
            super->x.HALF.HI = gTrinexx.bodyX;
            super->y.HALF.HI = gTrinexx.bodyY;
            // flushed with the colour of the head that is stunned
            {
                u32 pal = AP_TSHELL;
                if ((gTrinexx.stunned[0] || gTrinexx.stunned[1]) && (this->step & 8)) {
                    pal = gTrinexx.stunned[0] ? AP_TICE : AP_TFIRE;
                }
                if (pal != this->idx) {
                    this->idx = pal;
                    SetPalette(super, pal);
                }
            }
            SetFrame(super, i ? AF_TSHELL_R : AF_TSHELL_L);
            break;
        case BP_TNECK: {
            u32 head = i >> 3, n = (i & 7) * 2 + 1;
            s32 x, y;
            if (!gTrinexx.alive[head]) {
                DeleteThisEntity();
            }
            TrinexxNeckAt(head, n, &x, &y);
            super->x.HALF.HI = x;
            super->y.HALF.HI = y;
            super->spritePriority.b0 = 2;
            SetFrame(super, AF_TNECK);
            break;
        }
        case BP_TSEG:
            if (i >= 16) {
                // the rock head's neck, shown as it lunges
                s32 f = (i - 15);
                s32 bx = gTrinexx.bodyX, by = gTrinexx.bodyY + TRINEXX_HEAD_DY;
                super->x.HALF.HI = bx + (gTrinexx.headX - bx) * f / 4;
                super->y.HALF.HI = by + (gTrinexx.headY - by) * f / 4;
                super->spriteSettings.draw = gTrinexx.lunging;
                if (gTrinexx.lunging) {
                    COLLISION_ON(super);
                } else {
                    COLLISION_OFF(super);
                }
                SetFrame(super, AF_TSEG_1);
            } else {
                // the snake's body, along the trail its head left
                static const u8 back[TRINEXX_SEGS] = { 6, 12, 18, 24, 31, 38 };
                static const u16 frame[TRINEXX_SEGS] = { AF_TSEG_2, AF_TRINEXX_8, AF_TRINEXX_8, AF_TRINEXX_8,
                                                         AF_TSEG_2, AF_TSEG_1 };
                u32 h = (gTrinexx.hist - back[i]) & (TRINEXX_HIST - 1);
                super->x.HALF.HI = gTrinexx.histX[h];
                super->y.HALF.HI = gTrinexx.histY[h];
                super->spritePriority.b0 = 2 + (i > 2);
                if (i == 3) {
                    // the soft scale flashes
                    if ((this->step & 3) == 0) {
                        SetPalette(super, (this->step & 4) ? AP_TFIRE : AP_TRINEXX);
                    }
                    super->health = 255;
                    this->savedHealth = 255;
                }
                SetFrame(super, i == 5 ? AF_TSEG_0 : frame[i]);
            }
            break;
        case BP_CHAIN:
            super->x.HALF.HI = this->homeX;
            super->y.HALF.HI = this->homeY;
            SetFrame(super, AF_CHOMPLINK);
            break;
    }
}

static void BossPieceOnCollision(AlttpEnemy* this);
static void BossPieceOnDeath(AlttpEnemy* this) {
    super->health = 255;
    this->savedHealth = 255;
}

static void (*const BossPiece_Functions[])(AlttpEnemy*) = {
    BossPieceTick, BossPieceOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    BossPieceOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void BossPieceOnCollision(AlttpEnemy* this) {
    super->knockbackDuration = 0;
    if (super->health < this->savedHealth) {
        super->health = 255;
        if (this->cooldown == 0) {
            if (super->type == BP_TSEG && super->type2 == 3) {
                // a blow on the snake's soft scale goes to Trinexx
                gTrinexx.weakHits++;
                EnqueueSFX(SFX_BOSS_HIT);
            } else {
                EnqueueSFX(SFX_METAL_CLINK);
            }
            this->cooldown = 16;
        }
        super->iframes = -12;
    }
    EnemyFunctionHandlerAfterCollision(super, (void (*const*)())BossPiece_Functions);
}

void BossPiece(AlttpEnemy* this) {
    if (super->action != 0 && BossPieceArmored(this)) {
        EnemyFunctionHandler(super, (EntityActionArray)BossPiece_Functions);
    } else {
        BossPieceTick(this);
    }
}

/* ---- Freezor ------------------------------------------------------------------------
 * The Ice Palace's ghost of ice. It waits as a statue in the wall; when Link passes in
 * front, it shudders awake, pulls free and glides after him, glittering. Fire melts it
 * at once (the Fire Rod or Bombos), and when beaten it melts away.
 */
enum { FZ_INIT, FZ_STASIS, FZ_WAKE, FZ_MOVE, FZ_MELT };

static void FreezorTick(AlttpEnemy* this) {
    static const s8 vx[4] = { 8, -8, 0, 0 };
    static const s8 vy[4] = { 0, 0, 18, -18 };
    static const u8 glide[4] = { 1, 2, 1, 3 };
    if (super->action == FZ_INIT) {
        AlttpInit(this, AP_FREEZOR, AF_FREEZOR_0);
        super->action = FZ_STASIS;
        this->shield = 1;
        super->spriteSettings.shadow = 0;
    }
    this->step++;
    switch (super->action) {
        case FZ_STASIS:
            SetFrame(super, AF_FREEZOR_0);
            if (Abs(gPlayerEntity.base.x.HALF.HI - super->x.HALF.HI) < 16 &&
                Abs(gPlayerEntity.base.y.HALF.HI - super->y.HALF.HI) < 96) {
                super->action = FZ_WAKE;
                super->timer = 32;
                EnqueueSFX(SFX_ICE_BLOCK_SLIDE);
            }
            break;
        case FZ_WAKE:
            super->x.HALF.HI += (super->timer & 1) ? -1 : 1;
            if (--super->timer == 0) {
                static const s8 ox[4] = { 16, -16, 0, 0 };
                static const s8 oy[4] = { 0, 0, 16, -16 };
                u32 d;
                super->action = FZ_MOVE;
                super->timer = 80;
                // it pulls free of the wall toward the open floor (down, as a rule)
                this->face = 2;
                for (d = 0; d < 4 && WallAt(super, ox[2 ^ (d & 3)], oy[2 ^ (d & 3)]); d++) {
                }
                if (d < 4) {
                    this->face = 2 ^ d;
                }
                this->shield = 0;
                this->cooldown = 96;
                super->spriteSettings.shadow = 1;
            }
            break;
        case FZ_MOVE: {
            if (super->timer) {
                super->timer--;
            } else {
                this->face = Face4ToLink(super);
            }
            if (this->cooldown) {
                this->cooldown--;
                if (!(this->step & 7)) {
                    Entity* fx = CreateFx(super, FX_BLUE_SPARKLE, 0);
                    if (fx != NULL) {
                        fx->x.HALF.HI += (s32)(Random() & 15) - 4;
                        fx->y.HALF.HI -= 4;
                    }
                }
            }
            if (WallAt(super, 0, 0)) {
                // still in the wall it waited in (A Link to the Past opens the wall
                // there): it glides on out
                MoveVel(super, vx[this->face], vy[this->face]);
            } else {
                MoveVelWalls(super, vx[this->face], vy[this->face], 7);
            }
            SetFrame(super, this->face < 2 && (this->step & 16) ? AF_FREEZOR_LEAN : AF_FREEZOR_0 + glide[(this->step >> 2) & 3]);
            break;
        }
    }
    AlttpEndTick(this);
}

static void FreezorOnCollision(AlttpEnemy* this);
static void FreezorOnDeath(AlttpEnemy* this) {
    static const u8 melt[4] = { 7, 4, 5, 6 };
    if (super->action != FZ_MELT) {
        super->action = FZ_MELT;
        super->timer = 32;
        COLLISION_OFF(super);
        EnqueueSFX(SFX_ICE_BLOCK_MELT);
    }
    if (--super->timer == 0) {
        AlttpDeath(this, OCTOROK);
        return;
    }
    SetFrame(super, AF_FREEZOR_0 + melt[super->timer >> 3]);
}

static void (*const Freezor_Functions[])(AlttpEnemy*) = {
    FreezorTick, FreezorOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    FreezorOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void FreezorOnCollision(AlttpEnemy* this) {
    if (IsFire(super, super->contactedEntity) && super->action == FZ_MOVE) {
        super->health = 0;
    }
    super->knockbackDuration = 0;
    AlttpOnCollision(this, (void (*const*)())Freezor_Functions);
}

void Freezor(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Freezor_Functions);
}

/* ---- Pengator ------------------------------------------------------------------------
 * The Ice Palace's penguin. It turns to Link, waddles faster and faster, hops, and
 * belly-slides on across the ice until it hits a wall. Fields: face (0 right, 1 left,
 * 2 down, 3 up), pose its frame within the facing, homeX/Y its height and fall.
 */
enum { PG_INIT, PG_FACE, PG_SPEEDUP, PG_JUMP, PG_SLIDE };

static void PengatorTick(AlttpEnemy* this) {
    static const u8 base[4] = { 5, 0, 10, 15 };
    static const s8 wantX[4] = { 16, -16, 0, 0 };
    static const s8 wantY[4] = { 0, 0, 16, -16 };
    static const u8 jump[4] = { 4, 4, 3, 2 };
    s16* z = &this->homeX;
    s16* vz = &this->homeY;
    u32 hit;
    if (super->action == PG_INIT) {
        AlttpInit(this, AP_PENGATOR, AF_PENGATOR_0);
        super->action = PG_FACE;
        this->pose = 0;
        *z = *vz = 0;
        this->targetX = this->targetY = 0;
    }
    this->step++;
    MoveZ16(z, vz, 2, -64);
    hit = MoveVelWalls(super, this->targetX, this->targetY, 7);
    if (hit) {
        super->action = PG_FACE;
        this->targetX = this->targetY = 0;
    }
    switch (super->action) {
        case PG_FACE:
            this->face = Face4ToLink(super);
            super->action = PG_SPEEDUP;
            break;
        case PG_SPEEDUP:
            if (!(this->step & 3)) {
                bool32 more = FALSE;
                if (this->targetX != wantX[this->face]) {
                    this->targetX += wantX[this->face] > 0 ? 1 : -1;
                    more = TRUE;
                }
                if (this->targetY != wantY[this->face]) {
                    this->targetY += wantY[this->face] > 0 ? 1 : -1;
                    more = TRUE;
                }
                if (!more) {
                    super->action = PG_JUMP;
                    super->timer = 15;
                }
            }
            this->pose = (this->step & 4) >> 2;
            break;
        case PG_JUMP:
            if (super->timer == 0 || --super->timer == 0) {
                super->action = PG_SLIDE;
            } else if (super->timer == 5) {
                *vz = 24;
            }
            this->pose = jump[super->timer >> 2];
            break;
        case PG_SLIDE:
            if (!(this->step & 7) && *z == 0) {
                Entity* fx = CreateFx(super, FX_BLUE_SPARKLE, 0);
                if (fx != NULL) {
                    fx->y.HALF.HI += 4;
                }
            }
            break;
    }
    SetHeight(super, *z >> 4);
    SetFrame(super, AF_PENGATOR_0 + base[this->face] + this->pose);
    AlttpEndTick(this);
}

static void PengatorOnCollision(AlttpEnemy* this);
static void PengatorOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, OCTOROK);
}

static void (*const Pengator_Functions[])(AlttpEnemy*) = {
    PengatorTick, PengatorOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    PengatorOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void PengatorOnCollision(AlttpEnemy* this) {
    if (super->health < this->savedHealth) {
        super->action = PG_FACE;
        this->targetX = this->targetY = 0;
    }
    AlttpOnCollision(this, (void (*const*)())Pengator_Functions);
}

void Pengator(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Pengator_Functions);
}

/* ---- Stalfos Knight ---------------------------------------------------------------
 * The Ice Palace's big skeleton. It waits in the ceiling and drops when Link comes
 * under it, looks about, then hops at him again and again. Beaten, it falls to a heap
 * of bones (the head bobbing down onto them); a bomb (or the Hammer) finishes it then,
 * or else it shudders and pulls itself together again. Fields: head where it looks
 * (0 right, 1 left, 2 at Link), pose its body (0-4), unused[0] the head's drop,
 * homeX/Y its height and fall, rangeX the random stop, crusher frames before a hop,
 * unused[1] its full health and unused[2] what is left of it.
 */
enum { SK_INIT, SK_CEILING, SK_FALL, SK_SETTLE, SK_LOOK, SK_CROUCH, SK_HOP, SK_HEAP, SK_SHUDDER };

static void KnightGround(AlttpEnemy* this) {
    super->action = SK_SETTLE;
    super->timer = 63;
    this->homeX = this->homeY = 0;
    COLLISION_ON(super);
    this->shield = 0;
}

static void StalfosKnightTick(AlttpEnemy* this) {
    static const u8 lookAbout[16] = { 0, 0, 0, 2, 1, 1, 1, 2, 0, 0, 0, 2, 1, 1, 1, 2 };
    s16* z = &this->homeX;
    s16* vz = &this->homeY;
    u32 f;
    if (super->action == SK_INIT) {
        AlttpInit(this, AP_KNIGHT, AF_KNIGHT_0_2);
        super->action = SK_CEILING;
        super->spriteSettings.draw = 0;
        COLLISION_OFF(super);
        this->head = 2;
        this->pose = 2;
        this->unused[0] = 0;
        this->unused[1] = this->unused[2] = super->health; // what it pulls itself together with
        super->health = 255;
        *z = *vz = 0;
    }
    this->step++;
    switch (super->action) {
        case SK_CEILING:
            if (LinkNear(super, 20, 20)) {
                super->action = SK_FALL;
                super->spriteSettings.draw = 1;
                *z = 144 << 4;
                *vz = 0;
                EnqueueSFX(SFX_FALL_HOLE);
            }
            break;
        case SK_FALL:
            if (MoveZ16(z, vz, 3, -64)) {
                EnqueueSFX(SFX_THUD_HEAVY);
                KnightGround(this);
            }
            this->pose = 2;
            break;
        case SK_SETTLE:
            if (--super->timer == 0) {
                super->action = SK_LOOK;
                this->rangeX = Random() & 63;
                super->timer = 127;
            } else {
                this->pose = super->timer >> 5;
                this->head = 2;
            }
            break;
        case SK_LOOK:
            if (super->timer == this->rangeX) {
                this->head = gPlayerEntity.base.x.HALF.HI < super->x.HALF.HI;
                super->action = SK_CROUCH;
                super->timer = 32;
            } else {
                this->head = lookAbout[super->timer >> 3];
                this->pose = 0;
            }
            super->timer--;
            break;
        case SK_CROUCH:
            this->pose = 1;
            if (--super->timer == 0) {
                super->action = SK_HOP;
                super->timer = 255;
                this->crusher = 32;
            }
            break;
        case SK_HOP:
            if (super->timer) {
                super->timer--;
            }
            if (this->crusher) {
                if (--this->crusher == 0) {
                    *vz = 48;
                    AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI, 16, &this->targetX,
                           &this->targetY);
                    this->head = gPlayerEntity.base.x.HALF.HI < super->x.HALF.HI;
                    EnqueueSFX(SFX_PLY_JUMP);
                }
                this->pose = 1;
            } else {
                MoveVelWalls(super, this->targetX, this->targetY, 7);
                if (MoveZ16(z, vz, 2, -64)) {
                    if (super->timer == 0) {
                        KnightGround(this);
                    } else {
                        this->crusher = 16;
                    }
                }
                this->pose = *vz < 24 ? 2 : 0;
            }
            break;
        case SK_HEAP: {
            static const u8 drop[32] = { 0, 4, 8, 12, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
                                         14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 15, 14, 12, 8, 4, 0 };
            MoveZ16(z, vz, 2, -64);
            if (super->timer == 0 || --super->timer == 0) {
                if (Random() & 1) {
                    KnightGround(this);
                } else {
                    super->action = SK_SHUDDER;
                    super->timer = 80;
                }
                this->unused[2] = this->unused[1];
            } else {
                if (super->timer >= 224 && !(super->timer & 3)) {
                    EnqueueSFX(SFX_METAL_CLINK);
                }
                this->unused[0] = drop[super->timer >> 3];
                this->pose = 3;
                this->head = 2;
            }
            break;
        }
        case SK_SHUDDER:
            if (--super->timer == 0) {
                KnightGround(this);
            } else {
                this->pose = (super->timer >> 2) & 1 ? 4 : 1;
            }
            break;
    }
    SetHeight(super, *z >> 4);
    if (this->pose == 3) {
        u32 d = this->unused[0];
        f = d == 0 ? AF_KNIGHT_3_2 : d < 6 ? AF_KNIGHT_PILE_4 : d < 10 ? AF_KNIGHT_PILE_8 : d < 13 ? AF_KNIGHT_PILE_12 : AF_KNIGHT_PILE_14;
    } else if (this->pose == 2) {
        f = AF_KNIGHT_2;
    } else {
        static const u16 bodies[5] = { AF_KNIGHT_0_0, AF_KNIGHT_1_0, 0, 0, AF_KNIGHT_4_0 };
        f = bodies[this->pose] + this->head;
    }
    SetFrame(super, f);
    AlttpEndTick(this);
}

static void StalfosKnightOnCollision(AlttpEnemy* this);
static void StalfosKnightOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, OCTOROK);
}

static void (*const StalfosKnight_Functions[])(AlttpEnemy*) = {
    StalfosKnightTick, StalfosKnightOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    StalfosKnightOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void StalfosKnightOnCollision(AlttpEnemy* this) {
    Entity* by = super->contactedEntity;
    if (super->action == SK_HEAP) {
        // a heap of bones: only a bomb or the Hammer finishes it
        if (IsHeavy(by) || IsBlast(by)) {
            super->health = 0;
        } else {
            TookHit(this);
        }
    } else if (super->health < this->savedHealth) {
        // its own count of the blows (unused[2]): the engine's health stays high, so the
        // room never sees it at 0 between the blow and this
        u32 d = this->savedHealth - super->health;
        RestoreHealth(this);
        this->unused[2] = this->unused[2] > d ? this->unused[2] - d : 0;
        if (this->unused[2] == 0) {
            // beaten down, not out: it falls to pieces
            super->action = SK_HEAP;
            super->timer = 255;
            this->targetX = this->targetY = 0;
            EnqueueSFX(SFX_HIT);
        }
    }
    super->knockbackDuration = super->action == SK_HEAP ? 0 : super->knockbackDuration;
    AlttpOnCollision(this, (void (*const*)())StalfosKnight_Functions);
}

void StalfosKnight(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)StalfosKnight_Functions);
}

/* ---- Wizzrobe -----------------------------------------------------------------------
 * Misery Mire's wizards. Unseen, it waits, then flickers into sight facing Link, casts
 * a beam at him, and fades away again. It can only be hurt while it casts.
 */
enum { WZ_INIT, WZ_CLOAKED, WZ_PHASE_IN, WZ_ATTACK, WZ_PHASE_OUT };

static void WizzrobeTick(AlttpEnemy* this) {
    static const u8 dirGfx[4] = { 4, 2, 0, 6 };
    static const u8 attackGfx[8] = { 0, 1, 1, 1, 1, 1, 1, 0 };
    static const s8 beamV[6] = { 32, -32, 0, 0, 32, -32 };
    if (super->action == WZ_INIT) {
        AlttpInit(this, AP_WIZZROBE, AF_WIZZROBE_0);
        super->action = WZ_CLOAKED;
        super->timer = (Random() & 31) + 32;
        super->spriteSettings.draw = 0;
        COLLISION_OFF(super);
    }
    this->step++;
    switch (super->action) {
        case WZ_CLOAKED:
            super->spriteSettings.draw = 0;
            COLLISION_OFF(super);
            if (--super->timer == 0) {
                super->action = WZ_PHASE_IN;
                super->timer = 63;
                this->face = Face4ToLink(super);
                EnqueueSFX(SFX_APPARATE);
            }
            break;
        case WZ_PHASE_IN:
            super->spriteSettings.draw = super->timer & 1;
            if (--super->timer == 0) {
                super->action = WZ_ATTACK;
                super->timer = 63;
                COLLISION_ON(super);
            }
            SetFrame(super, AF_WIZZROBE_0 + dirGfx[this->face]);
            break;
        case WZ_ATTACK:
            super->spriteSettings.draw = 1;
            if (super->timer == 32) {
                AlttpEnemy* b = (AlttpEnemy*)SpawnBolt(super, BT_WIZZBEAM, 0, 0, beamV[this->face], beamV[this->face + 2]);
                if (b != NULL) {
                    b->idx = this->face;
                    b->base.collisionLayer = super->collisionLayer;
                    EnqueueSFX(SFX_ITEM_SWORD_BEAM);
                }
            }
            if (--super->timer == 0) {
                super->action = WZ_PHASE_OUT;
                super->timer = 63;
                COLLISION_OFF(super);
            }
            SetFrame(super, AF_WIZZROBE_0 + dirGfx[this->face] + attackGfx[super->timer >> 3]);
            break;
        case WZ_PHASE_OUT:
            super->spriteSettings.draw = super->timer & 1;
            if (--super->timer == 0) {
                super->action = WZ_CLOAKED;
                super->timer = (Random() & 31) + 32;
            }
            break;
    }
    AlttpEndTick(this);
}

static void WizzrobeOnCollision(AlttpEnemy* this);
static void WizzrobeOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, OCTOROK);
}

static void (*const Wizzrobe_Functions[])(AlttpEnemy*) = {
    WizzrobeTick, WizzrobeOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    WizzrobeOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void WizzrobeOnCollision(AlttpEnemy* this) {
    super->knockbackDuration = 0;
    AlttpOnCollision(this, (void (*const*)())Wizzrobe_Functions);
}

void Wizzrobe(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Wizzrobe_Functions);
}

/* ---- Laser Eye ----------------------------------------------------------------------
 * An eye in the wall (type: the way it looks, 0 right, 1 left, 2 down, 3 up). When Link
 * crosses its line it opens and fires a laser along it. Some only open while Link
 * looks at them (A Link to the Past's head_dir 0, from where they sit). They can't be
 * hurt.
 */
void LaserEye(AlttpEnemy* this) {
    static const s8 sx[4] = { 12, -4, 4, 4 };
    static const s8 sy[4] = { 4, 4, 12, -4 };
    static const s8 v[6] = { 112, -112, 0, 0, 112, -112 };
    static const u8 facing[4] = { 1, 0, 3, 2 }; // the way Link looks to face each eye
    u32 d = super->type & 3;
    s32 off;
    if (super->action == 0) {
        AlttpInit(this, AP_LASEREYE, AF_LASEREYE_0_0 + d * 2);
        super->spriteSettings.shadow = 0;
        // SpritePrep_LaserEye: from its place, whether it waits to be looked at
        this->head = d < 2 ? (super->y.HALF.HI >> 4) & 1 : ((super->x.HALF.HI >> 4) & 1) ^ 1;
        super->action = 1;
    }
    super->health = 0;
    super->knockbackDuration = 0;
    switch (super->action) {
        case 1:
            this->pose = 0;
            if (this->head == 0 && LinkFace4() != facing[d]) {
                break;
            }
            off = d < 2 ? gPlayerEntity.base.y.HALF.HI - super->y.HALF.HI : gPlayerEntity.base.x.HALF.HI - super->x.HALF.HI;
            if (off > -16 && off < 16 && InRoom(&gPlayerEntity.base, 0)) {
                super->action = 2;
                super->timer = 32;
            }
            break;
        case 2:
            this->pose = 1;
            if (--super->timer == 0) {
                AlttpEnemy* l = (AlttpEnemy*)SpawnBolt(super, BT_LASER, sx[d] - 8, sy[d] - 8, v[d], v[d + 2]);
                super->action = 3;
                super->timer = 12;
                if (l != NULL) {
                    l->idx = d;
                    // the eye sits in the wall, which can put it on the wall's layer:
                    // the beam runs along the floor Link stands on
                    l->base.collisionLayer = gPlayerEntity.base.collisionLayer;
                    EnqueueSFX(SFX_ITEM_SWORD_BEAM);
                }
            }
            break;
        case 3:
            if (--super->timer == 0) {
                super->action = 1;
            }
            break;
    }
    SetFrame(super, AF_LASEREYE_0_0 + d * 2 + this->pose);
}

/* ---- Chain Chomp --------------------------------------------------------------------
 * Turtle Rock's iron ball on a chain, staked to the floor (homeX/Y). It hops about the
 * stake, and every fourth time lunges at Link as far as the chain lets it, snapping
 * back. Nothing can hurt it. Its chain: four links (BOSS_PIECE), kept in order along
 * the child pointers.
 */
#define CHOMP_LINKS 4

static void ChompChain(AlttpEnemy* this, bool32 drawIn) {
    // ChainChomp_HandleLeash and _MoveChain: each link stays within 8 pixels of the
    // one before it, and creeps toward its place on the line to the stake
    AlttpEnemy* l = (AlttpEnemy*)this->child;
    s32 px = super->x.HALF.HI, py = super->y.HALF.HI - 4;
    s32 i = 1;
    while (l != NULL && i <= CHOMP_LINKS) {
        s32 dx = px - l->homeX, dy = py - l->homeY;
        if (dx > 8) {
            l->homeX = px - 8;
        } else if (dx < -8) {
            l->homeX = px + 8;
        }
        if (dy > 8) {
            l->homeY = py - 8;
        } else if (dy < -8) {
            l->homeY = py + 8;
        }
        if (drawIn) {
            s32 tx = this->homeX + (super->x.HALF.HI - this->homeX) * (CHOMP_LINKS + 1 - i) / (CHOMP_LINKS + 1);
            s32 ty = this->homeY + (super->y.HALF.HI - 4 - this->homeY) * (CHOMP_LINKS + 1 - i) / (CHOMP_LINKS + 1);
            l->homeX += l->homeX < tx ? 1 : l->homeX > tx ? -1 : 0;
            l->homeY += l->homeY < ty ? 1 : l->homeY > ty ? -1 : 0;
        }
        px = l->homeX;
        py = l->homeY;
        l = (AlttpEnemy*)l->child;
        i++;
    }
}

void ChainChomp(AlttpEnemy* this) {
    static const s8 vx[16] = { 0, 8, 11, 14, 16, 14, 11, 8, 0, -8, -11, -14, -16, -14, -11, -8 };
    static const s8 vy[16] = { -16, -14, -11, -8, 0, 8, 11, 14, 16, 14, 11, 8, 0, -9, -11, -14 };
    static const u8 gfx[16] = { 0, 1, 2, 3, 3, 3, 2, 1, 0, 0, 0, 4, 4, 4, 0, 0 };
    static const u8 flip[16] = { 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 };
    s16* z = (s16*)&this->unused[0];
    s16* vz = (s16*)&this->unused[2];
    bool32 inLeash;
    u32 d;
    if (super->action == 0) {
        AlttpEnemy* prev = this;
        u32 i;
        AlttpInit(this, AP_CHOMP, AF_CHOMP_0);
        super->action = 1;
        super->timer = 32;
        this->homeX = super->x.HALF.HI;
        this->homeY = super->y.HALF.HI;
        this->turns = 0;
        this->face = 8;
        this->targetX = this->targetY = 0;
        *z = *vz = 0;
        this->child = NULL;
        for (i = 0; i < CHOMP_LINKS; i++) {
            AlttpEnemy* l = (AlttpEnemy*)SpawnAt(BOSS_PIECE, BP_CHAIN, super, 0, 0);
            if (l == NULL) {
                break;
            }
            l->homeX = this->homeX;
            l->homeY = this->homeY;
            l->child = NULL;
            prev->child = &l->base;
            prev = l;
        }
        super->action = 1;
    }
    super->health = 0;
    super->knockbackDuration = 0;
    this->step++;
    ChompChain(this, FALSE);
    if (!(this->step & 3) && (this->targetX | this->targetY)) {
        this->face = ((AngleTo(this->targetX, this->targetY) + 8) >> 4) & 15;
    }
    MoveVel(super, this->targetX, this->targetY);
    MoveZ16(z, vz, 2, -64);
    inLeash = Abs(super->x.HALF.HI - this->homeX) < 48 && Abs(super->y.HALF.HI - this->homeY) < 48;
    switch (super->action) {
        case 1: // waiting, then a hop or a lunge
            if (super->timer == 0) {
                if (++this->turns == 4) {
                    this->turns = 0;
                    super->action = 3;
                    AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI, 64, &this->targetX,
                           &this->targetY);
                    EnqueueSFX(SFX_14C);
                } else {
                    u32 j = Random() & 15;
                    super->timer = (Random() & 31) + 16;
                    this->targetX = vx[j];
                    this->targetY = vy[j];
                    super->action = 2;
                }
            } else {
                super->timer--;
                this->targetX = this->targetY = 0;
            }
            break;
        case 2: // hopping about
            if (super->timer == 0 || --super->timer == 0) {
                super->timer = 32;
                super->action = 1;
            }
            if (!(super->timer & 15)) {
                ChompChain(this, TRUE);
            }
            if (*z == 0) {
                *vz = 16;
            }
            if (!inLeash) {
                AimVel(super, this->homeX, this->homeY, 16, &this->targetX, &this->targetY);
                MoveVel(super, this->targetX, this->targetY);
                super->timer = 12;
            }
            break;
        case 3: // the lunge, to the end of the chain
            if (!inLeash) {
                MoveVel(super, -this->targetX, -this->targetY);
                this->targetX = this->targetY = 0;
                super->action = 4;
                super->timer = 48;
                InitScreenShake(4, 0);
            }
            ChompChain(this, TRUE);
            ChompChain(this, TRUE);
            break;
        case 4: // jerked back, still
            if (--super->timer == 0) {
                super->action = 1;
                super->timer = 48;
            }
            ChompChain(this, TRUE);
            ChompChain(this, TRUE);
            break;
    }
    SetHeight(super, *z >> 4);
    d = this->face & 15;
    SetFrame(super, super->action == 3 ? AF_CHOMP_BITE : AF_CHOMP_0 + gfx[d] * 2 + flip[d]);
}

/* ---- Zazak ---------------------------------------------------------------------------
 * The lizard men of Thieves' Town. They walk a little, stop and look about (the head
 * turns first, the body follows), and every fourth stop look at Link. The red ones
 * (type 1) breathe a fireball when they face him. Fields: face the body, head the head
 * (0 right, 1 left, 2 down, 3 up), turns the stops, cooldown the open mouth.
 */
enum { ZZ_INIT, ZZ_WALK, ZZ_HALT, ZZ_SHOOT };

static void ZazakTick(AlttpEnemy* this) {
    static const u8 bodyGfx[8] = { 6, 4, 0, 2, 7, 5, 1, 3 };
    static const u8 stopFor[4] = { 16, 32, 64, 32 };
    static const s8 vx[4] = { 16, -16, 0, 0 };
    static const s8 vy[4] = { 0, 0, 16, -16 };
    static const u8 turn[8] = { 2, 3, 2, 3, 0, 1, 0, 1 };
    u32 g;
    if (super->action == ZZ_INIT) {
        AlttpInit(this, super->type ? AP_ZAZAK_RED : AP_ZAZAK_BLUE, AF_ZAZAK_0_0);
        super->action = ZZ_WALK;
        super->timer = 32;
        this->face = this->head = Face4ToLink(super);
        this->turns = 0;
        this->idx = 0;
        this->targetX = this->targetY = 0;
    }
    this->step++;
    if (this->cooldown) {
        this->cooldown--;
    }
    if (MoveVelWalls(super, this->targetX, this->targetY, 7) && super->action == ZZ_HALT) {
        super->timer = 0;
    }
    switch (super->action) {
        case ZZ_WALK: // a moment's look, then off the way the head looks
            if (super->timer == 0 || --super->timer == 0) {
                super->timer = stopFor[Random() & 3];
                super->action = ZZ_HALT;
                this->face = this->head;
                this->targetX = vx[this->face];
                this->targetY = vy[this->face];
            }
            break;
        case ZZ_HALT:
            if (super->timer) {
                super->timer--;
                if (++this->rangeX == 12) {
                    this->rangeX = 0;
                    this->idx++;
                }
                break;
            }
            this->targetX = this->targetY = 0;
            if (super->type == 1 && this->face == Face4ToLink(super)) {
                super->action = ZZ_SHOOT;
                super->timer = 48;
                this->cooldown = 48;
                break;
            }
            super->timer = 32;
            this->head = turn[this->face * 2 + (Random() & 1)];
            super->action = ZZ_WALK;
            if (++this->turns == 4) {
                this->turns = 0;
                this->head = Face4ToLink(super);
                super->timer = 24;
            }
            break;
        case ZZ_SHOOT:
            if (--super->timer == 0) {
                super->action = ZZ_WALK;
            } else if (super->timer == 24) {
                static const s8 fx[4] = { 16, -8, 4, 4 };
                static const s8 fy[4] = { -2, -2, 8, -20 };
                static const s8 fvx[4] = { 48, -48, 0, 0 };
                static const s8 fvy[4] = { 0, 0, 48, -48 };
                AlttpEnemy* b = (AlttpEnemy*)SpawnBolt(super, BT_PHLEGM, fx[this->face] - 4, fy[this->face] + 4,
                                                       fvx[this->face], fvy[this->face]);
                if (b != NULL) {
                    b->idx = this->face;
                    b->base.collisionLayer = super->collisionLayer;
                    EnqueueSFX(SFX_ITEM_LANTERN_ON);
                }
            }
            break;
    }
    g = bodyGfx[(this->idx & 1) * 4 + this->face];
    if (this->cooldown) {
        SetFrame(super, AF_ZAZAK_FIRE_0 + this->head);
    } else {
        SetFrame(super, AF_ZAZAK_0_0 + g * 4 + this->head);
    }
    AlttpEndTick(this);
}

static void ZazakOnCollision(AlttpEnemy* this);
static void ZazakOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, OCTOROK);
}

static void (*const Zazak_Functions[])(AlttpEnemy*) = {
    ZazakTick, ZazakOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    ZazakOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void ZazakOnCollision(AlttpEnemy* this) {
    AlttpOnCollision(this, (void (*const*)())Zazak_Functions);
}

void Zazak(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Zazak_Functions);
}

/* ---- Gibo ------------------------------------------------------------------------------
 * Thieves' Town's jelly (type 0): a wobbling shell nothing can hurt, which spits out
 * its nucleus (type 1) and creeps after it to swallow it again. The nucleus can be
 * beaten while it is out, and the shell goes with it. Fields (shell): child the
 * nucleus, unused[0] the nucleus's health, unused[1] set when it is beaten, turns
 * the spits (every third at Link).
 */
enum { GI_INIT, GI_EXPEL, GI_WAIT, GI_PURSUE };

static void GiboTick(AlttpEnemy* this) {
    static const s8 vx[8] = { 16, 16, 0, -16, -16, -16, 0, 16 };
    static const s8 vy[8] = { 0, 0, 16, -16, 16, 16, -16, -16 };
    if (super->type == 1) {
        // the nucleus: thrown out, it bounces about a moment and stops
        if (super->action == GI_INIT) {
            if (this->unused[0]) {
                super->health = this->unused[0]; // what it had left when swallowed
            }
            AlttpInit(this, AP_GIBOCORE, AF_GIBOCORE_0);
            super->action = GI_EXPEL;
            super->timer = 48;
            super->spritePriority.b0 = 1;
        }
        if (!ParentAlive(super)) {
            DeleteThisEntity();
        }
        this->step++;
        if (super->timer) {
            u32 hit;
            super->timer--;
            hit = MoveVelWalls(super, this->targetX, this->targetY, 6);
            if (hit & 1) {
                this->targetX = -this->targetX;
            }
            if (hit & 2) {
                this->targetY = -this->targetY;
            }
        }
        SetFrame(super, AF_GIBOCORE_0 + ((this->step >> 2) & 3));
        AlttpEndTick(this);
        return;
    }
    if (super->action == GI_INIT) {
        AlttpInit(this, AP_GIBO, AF_GIBO_0);
        super->action = GI_EXPEL;
        super->timer = 32;
        this->shield = 1;
        this->child = NULL;
        this->unused[0] = 0;
        this->unused[1] = 0;
        this->turns = 0;
        super->spritePriority.b0 = 2;
    }
    super->health = 255;
    this->savedHealth = 255;
    if (this->unused[1]) {
        super->health = 0;
        return;
    }
    this->step++;
    if (!(this->step & 63)) {
        this->face = (gPlayerEntity.base.x.HALF.HI < super->x.HALF.HI) << 2;
    }
    switch (super->action) {
        case GI_EXPEL:
            if (super->timer == 0 || --super->timer == 0) {
                AlttpEnemy* n = (AlttpEnemy*)SpawnAt(GIBO, 1, super, 0, 0);
                super->action = GI_WAIT;
                super->timer = 48;
                if (n != NULL) {
                    u32 i;
                    n->unused[0] = this->unused[0];
                    n->base.collisionLayer = super->collisionLayer;
                    if (++this->turns == 3) {
                        this->turns = 0;
                        i = (GetFacingDirection(super, &gPlayerEntity.base) + 2) >> 2 & 7;
                        AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI, 16, &n->targetX,
                               &n->targetY);
                    } else {
                        i = Random() & 7;
                        n->targetX = vx[i];
                        n->targetY = vy[i];
                    }
                    this->child = &n->base;
                    EnqueueSFX(SFX_14C);
                }
            }
            break;
        case GI_WAIT:
            if (--super->timer == 0) {
                super->action = GI_PURSUE;
            }
            break;
        case GI_PURSUE: {
            Entity* n = this->child;
            if (n == NULL || n->next == NULL || n->id != GIBO || n->parent != super) {
                super->action = GI_EXPEL;
                super->timer = 32;
                break;
            }
            if (Abs(n->x.HALF.HI - super->x.HALF.HI) < 3 && Abs(n->y.HALF.HI - super->y.HALF.HI) < 3) {
                // swallowed again
                this->unused[0] = n->health;
                DeleteEntity(n);
                this->child = NULL;
                super->action = GI_EXPEL;
                super->timer = (Random() & 31) + 32;
                break;
            }
            if (!(this->step & 3)) {
                AimVel(super, n->x.HALF.HI, n->y.HALF.HI, 16, &this->targetX, &this->targetY);
            }
            MoveVel(super, this->targetX, this->targetY);
            break;
        }
    }
    SetFrame(super, AF_GIBO_0 + this->face + ((this->step >> 3) & 3));
    AlttpEndTick(this);
}

static void GiboOnCollision(AlttpEnemy* this);
static void GiboOnDeath(AlttpEnemy* this) {
    if (super->type == 1) {
        AlttpEnemy* shell = (AlttpEnemy*)super->parent;
        if (ParentAlive(super)) {
            shell->unused[1] = 1;
        }
        CreateFx(super, FX_DEATH, 0);
        DeleteThisEntity();
    }
    AlttpDeath(this, OCTOROK);
}

static void (*const Gibo_Functions[])(AlttpEnemy*) = {
    GiboTick, GiboOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    GiboOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void GiboOnCollision(AlttpEnemy* this) {
    if (super->type == 0) {
        // the shell: nothing gets through
        super->knockbackDuration = 0;
        TookHit(this);
        EnemyFunctionHandlerAfterCollision(super, (void (*const*)())Gibo_Functions);
        return;
    }
    AlttpOnCollision(this, (void (*const*)())Gibo_Functions);
}

void Gibo(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Gibo_Functions);
}

/* ---- fire bar (Guruguru bar) -----------------------------------------------------------
 * A post with four fireballs on a bar, turning (type 0 clockwise, 1 the other way);
 * type 2 is one of its fireballs. Nothing about it can be hurt.
 */
void FireBar(AlttpEnemy* this) {
    if (super->type == 2) {
        AlttpEnemy* bar = (AlttpEnemy*)super->parent;
        s32 r;
        u32 a;
        if (super->action == 0) {
            AlttpInit(this, AP_FIREBAR, AF_FIREBAR_0);
            super->spriteSettings.shadow = 0;
        }
        if (!ParentAlive(super)) {
            DeleteThisEntity();
        }
        super->health = 0;
        super->knockbackDuration = 0;
        r = 16 * (super->type2 + 1);
        a = (u16)bar->targetX;
        super->x.HALF.HI = bar->base.x.HALF.HI + HalfSin(a, r);
        super->y.HALF.HI = bar->base.y.HALF.HI + HalfSin(a + 0x80, r);
        SetFrame(super, AF_FIREBAR_0 + ((bar->step >> 2) & 3));
        return;
    }
    if (super->action == 0) {
        u32 i;
        AlttpInit(this, AP_FIREBAR, AF_FIREBAR_0);
        super->spriteSettings.draw = 0;
        COLLISION_OFF(super);
        this->targetX = 0;
        for (i = 0; i < 4; i++) {
            Entity* f = SpawnAt(FIRE_BAR, 2, super, 0, 0);
            if (f != NULL) {
                f->type2 = i;
                f->collisionLayer = super->collisionLayer;
            }
        }
    }
    super->health = 0;
    this->step++;
    this->targetX = (this->targetX + (super->type ? -2 : 2)) & 0x1ff;
}

/* ---- the Stalfos head (sprite 0x7C) --------------------------------------------------
 * A floating skull in Misery Mire: it creeps up on Link while he looks away and backs
 * off when he turns to face it.
 */
static void StalfosHeadTick(AlttpEnemy* this) {
    static const u8 linkFaces[4] = { 1, 0, 3, 2 };
    if (super->action == 0) {
        AlttpInit(this, AP_SKULL, AF_SKULL_1);
        this->idx = 0;
        this->targetX = this->targetY = 0;
    }
    this->step++;
    if (LinkFace4() != linkFaces[Face4ToLink(super)]) {
        // Link looks away: it speeds up toward him
        if (!(this->step & 7)) {
            if (this->idx != 4) {
                this->idx++;
            }
            AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI, this->idx, &this->targetX,
                   &this->targetY);
        }
    } else if (!(this->step & 15)) {
        if (this->idx) {
            this->idx--;
        }
        AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI, this->idx, &this->targetX,
               &this->targetY);
    }
    MoveVelWalls(super, this->targetX * 4, this->targetY * 4, 6);
    SetHeight(super, 4 + ((this->step >> 4) & 1));
    SetFrame(super, gPlayerEntity.base.x.HALF.HI < super->x.HALF.HI ? AF_SKULL_1 : AF_SKULL_0);
    AlttpEndTick(this);
}

static void StalfosHeadOnCollision(AlttpEnemy* this);
static void StalfosHeadOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, OCTOROK);
}

static void (*const StalfosHead_Functions[])(AlttpEnemy*) = {
    StalfosHeadTick, StalfosHeadOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    StalfosHeadOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void StalfosHeadOnCollision(AlttpEnemy* this) {
    AlttpOnCollision(this, (void (*const*)())StalfosHead_Functions);
}

void StalfosHead(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)StalfosHead_Functions);
}

/* =============================================================================
 * Agahnim (sprite 336): Hyrule Castle tower's wizard, and in Ganon's Tower his second
 * fight with two shadows of himself.
 * ========================================================================== */

#include "save.h"

const Hitbox gHitbox_Agahnim = { 0, -12, { 6, 4, 4, 6 }, 9, 11 };

/* ---- Agahnim -------------------------------------------------------------------------
 * He hangs in the air facing Link while the spell grows between his hands, and lets
 * it go: an energy ball at Link (later on, now and then a seeker that bursts into six
 * sparks), and every fifth spell lightning down the room. Then he shrinks into his
 * warp shadow, glides off to another place in the room and rises out of it again.
 * Only his own balls, batted back with the sword, or the sword itself while he casts,
 * hurt him.
 *
 * Type 0 the castle tower's Agahnim (in the Dark World, as in Ganon's Tower, he is
 * Agahnim 2), type 1 Agahnim 2: he first splits off two shadows (type 2), who cast
 * and glide as he does but can't be hurt; balls batted back at a shadow fly on to
 * Agahnim himself.
 *
 * Fields: face the way he looks (A Link to the Past's sprite_D, 0 up, 3 down), idx the
 * spells cast (sprite_subtype), turns the damage taken, pose his pose, coerced the
 * spell's stage (sprite_head_dir), targetX/Y where he glides to, unused[0] the glide's
 * speed, unused[1] a shadow's side, cooldown frames he flinches, shield set while he
 * can't be hurt, child the spell in his hands, burst his fall.
 */
enum { AH_INIT, AH_APPEAR, AH_CAST, AH_VANISH, AH_GLIDE, AH_SPLIT, AH_SHADOW_OUT };
enum { AB_BALL, AB_SEEKER, AB_SPARK, AB_TRAIL, AB_GLOW };

#define AGAHNIM_HP 8
#define AGAHNIM2_HP 12

static bool32 AgahnimIsSecond(Entity* e) {
    return e->type == 1 || (e->type == 0 && BLEND_DARK_WORLD);
}

/** Damage to Agahnim (a ball batted back: 2, the sword: 1). FALSE while he can't be hurt. */
static bool32 AgahnimHurt(AlttpEnemy* this, u32 damage) {
    u32 hp = AgahnimIsSecond(super) ? AGAHNIM2_HP : AGAHNIM_HP;
    if (super->type == 2 || this->shield || super->health == 0) {
        return FALSE;
    }
    this->turns += damage;
    this->cooldown = 16;
    super->iframes = 24;
    EnqueueSFX(SFX_BOSS_HIT);
    if (this->turns >= hp) {
        super->health = 0;
        COLLISION_OFF(super);
    }
    return TRUE;
}

/** The six ways he looks (sprite_D) from where Link is (kAgahnim_Dir). */
static u32 AgahnimLook(Entity* e) {
    static const u8 dirs[25] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 0, 1, 1, 4, 4, 0, 2, 2, 4, 4, 3, 2, 2 };
    s16 vx, vy;
    AimVel(e, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI - 8, 2, &vx, &vy);
    vx = vx < -2 ? -2 : vx > 2 ? 2 : vx;
    vy = vy < -2 ? -2 : vy > 2 ? 2 : vy;
    return dirs[(vy + 2) * 5 + 2 + vx];
}

static AlttpEnemy* AgahnimSpawnBall(Entity* from, u32 type, s32 dx, s32 dy, s32 vx, s32 vy) {
    AlttpEnemy* b = (AlttpEnemy*)SpawnAt(AGAHNIM_BALL, type, from, dx, dy);
    if (b != NULL) {
        b->targetX = vx;
        b->targetY = vy;
        b->base.collisionLayer = from->collisionLayer;
        b->base.z.WORD = 0; // on Link's level, or his sword passes under it
        // the balls answer to the real Agahnim, whoever cast them
        if (from->kind == ENEMY && from->id == AGAHNIM && from->type == 2 && from->parent != NULL) {
            b->base.parent = from->parent;
        }
    }
    return b;
}

/** Agahnim_PerformAttack: a ball (or a seeker), or every fifth spell lightning. */
static void AgahnimAttack(AlttpEnemy* this) {
    static const s8 bx[6] = { 0, 10, 8, 0, -10, -10 };
    static const s8 by[6] = { -9, -2, -2, -9, -2, -2 };
    if (super->type != 2) {
        this->idx++;
    }
    if (this->idx == 5) {
        static const s8 lx[8] = { -8, 8, 8, -8, 8, -8, -8, 8 };
        u32 i;
        this->idx = 0;
        for (i = 0; i < 4; i++) {
            u32 r = Random() & 7;
            AlttpEnemy* b = (AlttpEnemy*)SpawnBolt(super, BT_LIGHTNING, lx[r], -2, 0, 0);
            if (b != NULL) {
                b->idx = r;
                b->base.collisionLayer = super->collisionLayer;
            }
        }
        EnqueueSFX(SFX_ITEM_SWORD_BEAM);
        EnqueueSFX(SFX_THUD_HEAVY);
    } else {
        s16 vx, vy;
        u32 seeker = this->idx >= 2 && (Random() & 1);
        AlttpEnemy* b;
        AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI - 4, 32, &vx, &vy);
        b = AgahnimSpawnBall(super, seeker ? AB_SEEKER : AB_BALL, bx[this->face], by[this->face] - 14, vx, vy);
        if (b != NULL && seeker) {
            b->base.timer = 32;
        }
        EnqueueSFX(SFX_ITEM_LANTERN_ON);
    }
}

/** The middle of the floor he was placed on, and how far he may glide from it
 * (rangeX/rangeY): the walls found either side, as for Blind. */
static s32 AgahnimScan(Entity* e, s32 sx, s32 sy) {
    s32 d;
    for (d = 8; d < 256; d += 8) {
        if (WallAt(e, sx * d, sy * d)) {
            return d;
        }
    }
    return 0;
}

static void AgahnimFindCenter(AlttpEnemy* this) {
    s32 l = AgahnimScan(super, -1, 0), r = AgahnimScan(super, 1, 0);
    s32 u = AgahnimScan(super, 0, -1), d = AgahnimScan(super, 0, 1);
    s32 w, h;
    this->homeX = super->x.HALF.HI;
    this->homeY = super->y.HALF.HI;
    if (l == 0 || r == 0) {
        l = r = 120;
    }
    if (u == 0 || d == 0) {
        u = d = 96;
    }
    this->homeX += (r - l) / 2;
    this->homeY += (d - u) / 2;
    w = (l + r) / 2 - 24;
    h = (u + d) / 2 - 20;
    this->rangeX = w < 16 ? 16 : w > 96 ? 96 : w;
    this->rangeY = h < 16 ? 16 : h > 80 ? 80 : h;
}

/** Somewhere on the floor to glide to (kAgahnim_Tab3/4 about its middle), clear of
 * walls and not on top of Link. */
static void AgahnimPickSpot(AlttpEnemy* this) {
    static const u8 tx[16] = { 0x38, 0x38, 0x38, 0x58, 0x78, 0x98, 0xb8, 0xb8, 0xb8, 0x98, 0x58, 0x58, 0x60, 0x90, 0x98, 0x78 };
    static const u8 ty[16] = { 0xb8, 0x78, 0x58, 0x48, 0x48, 0x48, 0x58, 0x78, 0xb8, 0xb8, 0xb8, 0x90, 0x70, 0x70, 0x90, 0xa0 };
    u32 j = Random() & 15, k;
    s32 x = this->homeX, y = this->homeY;
    for (k = 0; k < 16; k++, j = (j + 5) & 15) {
        // the table spans 0x40 either way across and 0x38 down; scaled to this floor
        x = this->homeX + (tx[j] - 0x78) * this->rangeX / 0x40;
        y = this->homeY + (ty[j] - 0x80) * this->rangeY / 0x38;
        if (GetCollisionDataAtWorldCoords(x, y, super->collisionLayer) == 0 &&
            GetCollisionDataAtWorldCoords(x, y - 16, super->collisionLayer) == 0 &&
            Distance(x - gPlayerEntity.base.x.HALF.HI, y - gPlayerEntity.base.y.HALF.HI) > 40) {
            break;
        }
    }
    this->targetX = x;
    this->targetY = y;
}

static void AgahnimSetPalette(AlttpEnemy* this) {
    static const u8 pals[3] = { AP_AGAHNIM_DW, AP_AGAHNIM_SHADOW1, AP_AGAHNIM_SHADOW2 };
    if (super->type == 2) {
        SetPalette(super, pals[1 + (this->unused[1] & 1)]);
    } else {
        SetPalette(super, AgahnimIsSecond(super) ? pals[0] : AP_AGAHNIM);
    }
}

static void AgahnimTick(AlttpEnemy* this) {
    static const u8 gfx1[6] = { 2, 10, 8, 0, 4, 6 };
    static const u8 hands[16] = { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0 };
    static const u8 stage[16] = { 0, 0, 0, 0, 0, 0, 0, 6, 5, 4, 3, 2, 1, 0, 0, 0 };
    static const u8 stageBase[6] = { 30, 24, 12, 0, 6, 18 };
    u32 t;
    if (super->action == AH_INIT) {
        AlttpInit(this, AP_AGAHNIM, AF_AGAHNIM_0);
        AgahnimSetPalette(this);
        super->spritePriority.b0 = 3;
        this->face = 3;
        this->idx = this->turns = this->coerced = this->cooldown = 0;
        this->burst = 0;
        if (super->type != 2) {
            Entity* glow;
            AgahnimFindCenter(this);
            ((Enemy*)this)->enemyFlags |= EM_FLAG_BOSS;
            if (AgahnimIsSecond(super)) {
                // Agahnim 2: shows himself, then splits off his shadows
                super->action = AH_SPLIT;
                super->timer = 80;
                this->pose = AF_AGAHNIM_0;
            } else {
                super->action = AH_CAST;
                super->timer = 255;
            }
            this->shield = 1;
            glow = SpawnAt(AGAHNIM_BALL, AB_GLOW, super, 0, 0);
            if (glow != NULL) {
                glow->collisionLayer = super->collisionLayer;
            }
        } else {
            // a shadow, sliding out sideways from Agahnim
            AlttpEnemy* ag = (AlttpEnemy*)super->parent;
            super->action = AH_SHADOW_OUT;
            super->timer = 32;
            this->homeX = ag->homeX;
            this->homeY = ag->homeY;
            this->rangeX = ag->rangeX;
            this->rangeY = ag->rangeY;
            this->shield = 1;
            this->targetY = 0;
            COLLISION_OFF(super);
            {
                Entity* glow = SpawnAt(AGAHNIM_BALL, AB_GLOW, super, 0, 0);
                if (glow != NULL) {
                    glow->collisionLayer = super->collisionLayer;
                }
            }
        }
    }
    this->step++;
    if (this->cooldown) {
        this->cooldown--;
    }
    this->coerced = 0;
    t = super->timer;
    switch (super->action) {
        case AH_SPLIT:
            this->pose = AF_AGAHNIM_0;
            super->spriteSettings.draw = 1;
            if (t == 64) {
                u32 i;
                EnqueueSFX(SFX_SUMMON);
                for (i = 0; i < 2; i++) {
                    AlttpEnemy* s = (AlttpEnemy*)SpawnAt(AGAHNIM, 2, super, 0, 0);
                    if (s != NULL) {
                        s->unused[1] = i;
                        s->base.collisionLayer = super->collisionLayer;
                    }
                }
            }
            if (--super->timer == 0) {
                super->action = AH_CAST;
                super->timer = 32;
            }
            break;
        case AH_SHADOW_OUT:
            // kAgahnim_Tab5: out to the side, sinking faster and faster
            this->targetY += 2;
            MoveVel(super, this->unused[1] ? -32 : 32, this->targetY);
            {
                s32 lx = gRoomControls.origin_x + 24, rx = gRoomControls.origin_x + gRoomControls.width - 24;
                s32 by = gRoomControls.origin_y + gRoomControls.height - 24;
                if (super->x.HALF.HI < lx) {
                    super->x.HALF.HI = lx;
                }
                if (super->x.HALF.HI > rx) {
                    super->x.HALF.HI = rx;
                }
                if (super->y.HALF.HI > by) {
                    super->y.HALF.HI = by;
                }
            }
            this->pose = AF_AGAHNIM_0;
            super->spriteSettings.draw = this->step & 1;
            if (--super->timer == 0) {
                super->action = AH_CAST;
                super->timer = 32;
            }
            break;
        case AH_CAST: {
            u32 j = t >> 4;
            // the first and last sixteen frames he flickers in and out of sight
            bool32 solid = t < 239 && t >= 16;
            super->spriteSettings.draw = solid || (this->step & 1);
            this->shield = !solid || super->type == 2;
            if (super->type != 2) {
                if (solid) {
                    COLLISION_ON(super);
                } else {
                    COLLISION_OFF(super);
                }
            }
            if (t >= 128) {
                this->face = AgahnimLook(super);
            } else if (t == 112) {
                AgahnimAttack(this);
            }
            this->coerced = stage[j] ? stage[j] + stageBase[this->face] : 0;
            this->pose = AF_AGAHNIM_0 + gfx1[this->face] + hands[j];
            if (this->idx == 4 && hands[j] && super->type != 2) {
                // he stretches up as he calls the lightning down
                this->pose = AF_AGAHNIM_CALL;
            }
            if (--super->timer == 0) {
                super->action = AH_VANISH;
                super->timer = 51;
                this->shield = 1;
                COLLISION_OFF(super);
                EnqueueSFX(SFX_EVAPORATE);
            }
            break;
        }
        case AH_VANISH: {
            // the robe thins into his warp shadow, which shrinks to a dot
            static const u8 shrink[5] = { 4, 3, 2, 1, 0 };
            super->spriteSettings.draw = 1;
            if (t >= 40) {
                this->pose = AF_AGAHNIM_FADE_1 + ((51 - t) >> 2);
            } else {
                this->pose = AF_AGAHNIM_WARP_0 + 4 - shrink[t >> 3];
            }
            if (--super->timer == 0) {
                super->action = AH_GLIDE;
                this->unused[0] = 8;
                AgahnimPickSpot(this);
            }
            break;
        }
        case AH_GLIDE: {
            s32 dx = this->targetX - super->x.HALF.HI, dy = this->targetY - super->y.HALF.HI;
            this->pose = AF_AGAHNIM_WARP_4;
            if (Abs(dx) < 7 && Abs(dy) < 7) {
                super->x.HALF.HI = this->targetX;
                super->y.HALF.HI = this->targetY;
                super->action = AH_APPEAR;
                super->timer = 51;
                EnqueueSFX(SFX_APPARATE);
            } else {
                s16 vx, vy;
                AimVel(super, this->targetX, this->targetY, this->unused[0], &vx, &vy);
                MoveVel(super, vx, vy);
                if (this->unused[0] < 64) {
                    this->unused[0]++;
                }
            }
            break;
        }
        case AH_APPEAR:
            // the shadow grows, and he rises out of it
            if (t >= 12) {
                this->pose = AF_AGAHNIM_WARP_0 + 4 - ((t - 12) >> 3);
            } else {
                this->pose = AF_AGAHNIM_FADE_1 + (t >> 2);
            }
            if (--super->timer == 0) {
                super->action = AH_CAST;
                super->timer = 255;
            }
            break;
    }
    if (this->cooldown && super->action == AH_CAST) {
        this->pose = AF_AGAHNIM_HURT;
    }
    // he floats, bobbing gently (not while a warp shadow on the floor)
    SetHeight(super, (this->pose >= AF_AGAHNIM_WARP_0 && this->pose <= AF_AGAHNIM_WARP_4) ? 0
                                                                                      : 3 + ((this->step >> 4) & 1));
    super->spriteSettings.shadow = !(this->pose >= AF_AGAHNIM_WARP_0 && this->pose <= AF_AGAHNIM_WARP_4);
    SetFrame(super, this->pose);
    AlttpEndTick(this);
}

static void AgahnimOnCollision(AlttpEnemy* this);
static void AgahnimOnDeath(AlttpEnemy* this) {
    // he falls in a heap, flickers and is gone; his shadows and spells with him
    if (this->burst == 0) {
        DeleteChildren(super);
        COLLISION_OFF(super);
        SetHeight(super, 0);
        SetFrame(super, AF_AGAHNIM_DEAD);
        EnqueueSFX(SFX_BOSS_DIE);
    }
    if (++this->burst < 112) {
        super->spriteSettings.draw = this->burst < 48 || ((this->burst >> 1) & 1);
        if ((this->burst & 15) == 0) {
            Entity* fx = CreateFx(super, FX_DEATH, 0);
            if (fx != NULL) {
                fx->x.HALF.HI += (Random() & 15) - 8;
                fx->y.HALF.HI += (Random() & 7) - 8;
            }
        }
        return;
    }
    CreateFx(super, FX_GIANT_EXPLOSION4, 0);
    EnqueueSFX(SFX_BOSS_EXPLODE);
    DeleteThisEntity();
}

static void (*const Agahnim_Functions[])(AlttpEnemy*) = {
    AgahnimTick, AgahnimOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    AgahnimOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void AgahnimOnCollision(AlttpEnemy* this) {
    super->knockbackDuration = 0;
    if (TookHit(this)) {
        if (!AgahnimHurt(this, 1)) {
            EnqueueSFX(SFX_METAL_CLINK);
        }
    }
    EnemyFunctionHandlerAfterCollision(super, (void (*const*)())Agahnim_Functions);
}

void Agahnim(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Agahnim_Functions);
    BossCam(super, -8);
}

/* ---- his balls (AGAHNIM_BALL) ----------------------------------------------------------
 * Sprite_7B_AgahnimBalls. The energy ball flies straight, a trail of smaller balls
 * behind it; a blow of the sword bats it back at Agahnim, where it strikes him (or
 * flies past while he is a warp shadow). The seeker homes in on Link for half a second
 * and bursts into six sparks on a wall or a blow. Fields: targetX/Y the speed, idx 1
 * once batted back, step its clock.
 */
static void AgahnimSparks(Entity* from) {
    static const s8 vx[6] = { 0, 24, 24, 0, -24, -24 };
    static const s8 vy[6] = { -32, -16, 16, 32, 16, -16 };
    u32 i;
    for (i = 0; i < 6; i++) {
        AlttpEnemy* s = AgahnimSpawnBall(from, AB_SPARK, 0, 0, vx[i], vy[i]);
        if (s != NULL) {
            s->base.parent = from->parent;
        }
    }
    EnqueueSFX(SFX_ITEM_BOMB_EXPLODE);
}

static void AgahnimBallTick(AlttpEnemy* this) {
    static const u8 trailGfx[16] = { 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 0, 0, 0, 0 };
    u32 type = super->type;
    if (super->action == 0) {
        AlttpInit(this, AP_AGBALL_A, type == AB_GLOW ? AF_AGGLOW_0 : AF_AGBALL_0);
        super->spriteSettings.shadow = 0;
        super->spritePriority.b0 = type == AB_GLOW ? 2 : 3;
        this->step = 0;
        this->idx = 0;
        if (type == AB_TRAIL || type == AB_GLOW) {
            COLLISION_OFF(super);
        }
    }
    this->step++;
    super->knockbackDuration = 0;
    switch (type) {
        case AB_GLOW: {
            // the spell between Agahnim's hands: it follows him, and grows
            AlttpEnemy* ag = (AlttpEnemy*)super->parent;
            super->health = 0;
            if (!ParentAlive(super) || super->parent->id != AGAHNIM) {
                DeleteThisEntity();
            }
            CopyPosition(&ag->base, super);
            super->spriteSettings.draw = ag->coerced != 0 && ag->base.spriteSettings.draw;
            if (ag->coerced) {
                SetFrame(super, AF_AGGLOW_0 + ag->coerced - 1);
            }
            if (!(this->step & 1)) {
                SetPalette(super, (this->step & 2) ? AP_AGBALL_B : AP_AGBALL_A);
            }
            return;
        }
        case AB_TRAIL:
            super->health = 0;
            if (super->timer == 0 || --super->timer == 0) {
                DeleteThisEntity();
            }
            SetFrame(super, AF_AGBALL_0 + trailGfx[super->timer]);
            return;
        case AB_SPARK:
            super->health = 0;
            MoveVel(super, this->targetX, this->targetY);
            SetFrame(super, AF_AGBALL_SPARK);
            if (!(this->step & 3)) {
                SetPalette(super, (this->step & 4) ? AP_AGBALL_B : AP_AGBALL_A);
            }
            if (WallAt(super, 0, 0) || !InRoom(super, 0) || this->step > 120) {
                CreateFx(super, FX_SPARKLE2, 0);
                DeleteThisEntity();
            }
            return;
    }
    // the energy ball and the seeker
    if (type == AB_SEEKER && super->timer) {
        super->timer--;
        AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI - 4, 32, &this->targetX,
               &this->targetY);
    }
    MoveVel(super, this->targetX, this->targetY);
    if (this->idx) {
        // batted back: it strikes the real Agahnim if it reaches him
        Entity* ag = super->parent;
        if (ag != NULL && ag->next != NULL && ag->kind == ENEMY && ag->id == AGAHNIM && ag->health != 0 &&
            Abs(ag->x.HALF.HI - super->x.HALF.HI) < 14 && Abs(ag->y.HALF.HI - 12 - super->y.HALF.HI) < 14 &&
            AgahnimHurt((AlttpEnemy*)ag, 2)) {
            CreateFx(super, FX_SPARKLE2, 0);
            DeleteThisEntity();
        }
    }
    if (WallAt(super, 0, 0) || !InRoom(super, 0) || this->step > 240) {
        if (type == AB_SEEKER) {
            AgahnimSparks(super);
        } else {
            CreateFx(super, FX_SPARKLE2, 0);
        }
        DeleteThisEntity();
    }
    if (type == AB_SEEKER) {
        SetFrame(super, AF_AGBALL_SEEK_0 + ((this->step >> 2) & 1));
        if (this->step == 1) {
            SetPalette(super, AP_AGBALL_B);
        }
    } else {
        SetFrame(super, AF_AGBALL_0);
        if (!(this->step & 1)) {
            SetPalette(super, (this->step & 2) ? AP_AGBALL_B : AP_AGBALL_A);
        }
        if (!(this->step & 3)) {
            Entity* tr = SpawnAt(AGAHNIM_BALL, AB_TRAIL, super, 0, 0);
            if (tr != NULL) {
                tr->timer = 15;
                tr->parent = super->parent;
                tr->collisionLayer = super->collisionLayer;
            }
        }
    }
    AlttpEndTick(this);
}

static void AgahnimBallOnCollision(AlttpEnemy* this);
static void AgahnimBallOnDeath(AlttpEnemy* this) {
    DeleteThisEntity();
}

static void (*const AgahnimBall_Functions[])(AlttpEnemy*) = {
    AgahnimBallTick, AgahnimBallOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    AgahnimBallOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void AgahnimBallOnCollision(AlttpEnemy* this) {
    super->knockbackDuration = 0;
    if ((super->type == AB_BALL || super->type == AB_SEEKER) && TookHit(this)) {
        if (super->type == AB_SEEKER) {
            AgahnimSparks(super);
            DeleteThisEntity();
        } else if (!this->idx) {
            // batted back at Agahnim, faster
            Entity* ag = super->parent;
            this->idx = 1;
            COLLISION_OFF(super);
            EnqueueSFX(SFX_ITEM_SHIELD_BOUNCE);
            if (ag != NULL && ag->next != NULL && ag->kind == ENEMY && ag->id == AGAHNIM) {
                AimVel(super, ag->x.HALF.HI, ag->y.HALF.HI - 12, 48, &this->targetX, &this->targetY);
            } else {
                this->targetX = -this->targetX * 3 / 2;
                this->targetY = -this->targetY * 3 / 2;
            }
        }
    }
    super->iframes = 0;
    EnemyFunctionHandlerAfterCollision(super, (void (*const*)())AgahnimBall_Functions);
}

void AgahnimBall(AlttpEnemy* this) {
    if (this->base.type >= AB_SPARK) {
        // sparks, trails and the glow never take a blow
        AgahnimBallTick(this);
        return;
    }
    EnemyFunctionHandler(super, (EntityActionArray)AgahnimBall_Functions);
}

/* =============================================================================
 * Ganon (sprite 337): the last fight, in the Pyramid of Power.
 * ========================================================================== */

#include "screen.h"
#include "flags.h"
#include "area.h"

const Hitbox gHitbox_Ganon = { 0, -12, { 8, 6, 6, 8 }, 14, 14 };
const Hitbox gHitbox_GanonBat = { 0, -4, { 6, 4, 4, 6 }, 8, 5 };

/** What his fight keeps between its pieces: his torches and the darkness. */
typedef struct {
    u8 dark;          /**< set once he has put the lights out */
    u8 level;         /**< the darkness now (0 none, 16 black), eased toward its goal */
    u16 burn[2];      /**< frames each torch burns on (0: dark); 0xffff burns for good */
    u16 angle;        /**< his ring of fire bats turns: 0x200 a full turn */
    u8 radius;        /**< and how wide it is */
    u8 bats;          /**< bats spawned into the ring */
} GanonState;
#define gGanon (*(GanonState*)(gAlttpEnemyState + 0xd00))

/* ---- Ganon --------------------------------------------------------------------------
 * Sprite_D6_Ganon, with his trident (Sprite_GanonTrident) and fire bats
 * (Sprite_FireBat_*). In four phases, by the blows he has taken:
 *   1. He spins his trident overhead and hurls it; it hunts Link, then flies back to
 *      his hand while he warps across the room, leaving afterimages.
 *   2. He whirls a ring of fire bats about the trident, wider and narrower, then lets
 *      them go at Link one by one.
 *   3. He sends fire bats spiralling out about him, trailing flames, now and then
 *      bats in a ring again. Struck, he leaps and slams the floor (four times).
 *   4. He puts the torches out: in the dark he can't be seen, and only shows while a
 *      torch burns (the Fire Rod or the lit lantern lights them; they burn out), and
 *      can only be struck with both lit. A sword blow stuns him blue for a moment:
 *      then only the silver arrows (arrows of the Light Arrow bow) hurt him.
 * He takes no other harm: in phases 1-3 the sword's blows only count toward the next
 * phase.
 *
 * Fields: face his side (sprite_D: 0 facing down from the top of the room, 1 up from
 * the bottom), head his head (0, 1 ahead, 2), pose his pose (0-16, POSE_* added, 0xff
 * hidden), idx where he warps to (sprite_subtype, 0-7), turns blows taken, crusher
 * the glide's kind (the ALttP state it ends in), coerced the trident's pose while he
 * holds it (sprite_G), rangeX/Y and homeX/Y his floor, unused[0] slams, unused[1]
 * silver arrows taken, cooldown the flicker after a blow, step his clock, child his
 * trident.
 */
enum {
    GN_INIT, GN_INTRO, GN_THROW, GN_WAIT_TRIDENT, GN_CATCH, GN_GLIDE, GN_SHAKE, GN_RING, GN_RING_SWIRL, GN_RING_LET_GO,
    GN_SHOW, GN_SPIRAL, GN_SHOW3, GN_LEAP, GN_SLAM, GN_DARK, GN_STUN,
};
enum { GP_TRIDENT, GP_BAT, GP_SPIRAL_BAT, GP_TRAIL_BAT, GP_FLAME, GP_TORCH, GP_TORCH_FIRE, GP_IMAGE };
enum { POSE_HOLD = 17, POSE_CROUCH, POSE_SPRING, POSE_FALL, POSE_HIDDEN = 0xff };

#define GANON_PHASE1_HITS 4
#define GANON_PHASE2_HITS 8
#define GANON_SLAMS 4
#define GANON_ARROWS 4

static u32 GanonPhase(AlttpEnemy* this) {
    if (this->turns < GANON_PHASE1_HITS) {
        return 1;
    }
    if (this->turns < GANON_PHASE2_HITS) {
        return 2;
    }
    return this->unused[0] < GANON_SLAMS ? 3 : 4;
}

/** His ALttP position (the middle of him; his frames stand 12 pixels lower). */
#define GANON_Y(e) ((e)->y.HALF.HI - 12)

static u32 GanonLitTorches(void) {
    return (gGanon.burn[0] != 0) + (gGanon.burn[1] != 0);
}

/** The darkness on the room's backgrounds (the sprites stay bright), and its end. */
static void GanonDarkness(s32 level) {
    if (level <= 0) {
        if (gScreen.controls.layerFXControl ==
            (BLDCNT_TGT1_BG1 | BLDCNT_TGT1_BG2 | BLDCNT_TGT1_BG3 | BLDCNT_TGT1_BD | BLDCNT_EFFECT_DARKEN)) {
            gScreen.controls.layerFXControl = 0;
            gScreen.controls.layerBrightness = 0;
        }
        return;
    }
    gScreen.controls.layerFXControl =
        BLDCNT_TGT1_BG1 | BLDCNT_TGT1_BG2 | BLDCNT_TGT1_BG3 | BLDCNT_TGT1_BD | BLDCNT_EFFECT_DARKEN;
    gScreen.controls.layerBrightness = level;
}

/** Each frame: the room as dark as the torches leave it (LayerEffect_Ganon). */
static void GanonUpdateLight(void) {
    static const u8 goal[3] = { 14, 9, 0 };
    u32 want = gGanon.dark ? goal[GanonLitTorches()] : 0;
    if (gGanon.level < want) {
        gGanon.level++;
    } else if (gGanon.level > want) {
        gGanon.level--;
    }
    GanonDarkness(gGanon.level);
}

/** Called when the room changes: the lights come back on. */
void GanonLightsReset(void) {
    if (gGanon.dark || gGanon.level) {
        gGanon.dark = 0;
        gGanon.level = 0;
        GanonDarkness(0);
    }
}

static bool32 GanonIsSilverArrow(Entity* e) {
    return e != NULL && e->kind == PLAYER_ITEM && e->id == PLAYER_ITEM_BOW &&
           (e->hurtType == 0x0e || GetInventoryValue(ITEM_LIGHT_ARROW));
}

static Entity* GanonSpawnPart(AlttpEnemy* this, u32 type, s32 dx, s32 dy) {
    Entity* e = SpawnAt(GANON_PART, type, super, dx, dy);
    if (e != NULL) {
        e->collisionLayer = super->collisionLayer;
    }
    return e;
}

/** Ganon_SelectWarpLocation: the next spot, across the room (top row to bottom). */
static void GanonWarp(AlttpEnemy* this, u32 kind) {
    static const u8 nextY[8] = { 0x40, 0x30, 0x30, 0x40, 0xb0, 0xc0, 0xc0, 0xb0 };
    static const u8 nextX[8] = { 0x30, 0x50, 0xa0, 0xc0, 0x40, 0x60, 0x90, 0xb0 };
    u32 j = (Random() & 3) | (this->idx < 4 ? 4 : 0);
    s32 k;
    this->idx = j;
    // the table is about the middle of his 256-pixel hall (0x78, 0x78); scaled to this floor
    this->targetX = this->homeX + (nextX[j] - 0x78) * this->rangeX / 88;
    this->targetY = this->homeY + (nextY[j] - 0x78) * this->rangeY / 76;
    // clear of the walls: his feet, his shoulders and the top of his head
    for (k = 0; k < 16; k++) {
        s32 x = this->targetX, y = this->targetY;
        if (!GetCollisionDataAtWorldCoords(x, y + 8, super->collisionLayer) &&
            !GetCollisionDataAtWorldCoords(x - 20, y - 8, super->collisionLayer) &&
            !GetCollisionDataAtWorldCoords(x + 20, y - 8, super->collisionLayer) &&
            !GetCollisionDataAtWorldCoords(x, y - 36, super->collisionLayer)) {
            break;
        }
        this->targetX += this->targetX < this->homeX ? 4 : this->targetX > this->homeX ? -4 : 0;
        this->targetY += this->targetY < this->homeY ? 4 : this->targetY > this->homeY ? -4 : 0;
    }
    this->crusher = kind;
    super->action = GN_GLIDE;
    super->timer = 48;
    COLLISION_OFF(super);
    EnqueueSFX(SFX_TELEPORTER);
}

static void GanonIdle(AlttpEnemy* this) {
    this->coerced = this->face ? 10 : 9;
    this->pose = this->face ? 10 : POSE_HOLD;
}

/** Ganon_Phase1_AnimateTridentSpin: the trident whirled overhead. */
static void GanonSpin(AlttpEnemy* this) {
    static const u8 g[8] = { 8, 7, 6, 5, 4, 3, 2, 1 };
    static const u8 gfx[8] = { 0, 0, 1, 1, 0, 0, 1, 1 };
    u32 j = (super->timer >> 2) & 7;
    this->coerced = g[j];
    this->pose = gfx[j] + (this->face ? 8 : 0);
    if (!(this->step & 15)) {
        EnqueueSFX(SFX_ITEM_GLOVES_AIR);
    }
}

static void GanonShakeHead(AlttpEnemy* this) {
    static const u8 dirs[16] = { 0, 0, 0, 1, 2, 2, 2, 1, 0, 0, 0, 1, 1, 1, 1, 1 };
    this->head = dirs[(super->timer >> 3) & 15];
}

/** Ganon_HandleFireBatCircle: the ring about the trident in his raised hand. */
static void GanonRing(AlttpEnemy* this) {
    gGanon.angle -= 4;
}

/** Ganon_Func1: a fire bat flung from him, spiralling (3) or straight at Link (5). */
static void GanonFling(AlttpEnemy* this, u32 type) {
    AlttpEnemy* b = (AlttpEnemy*)GanonSpawnPart(this, type, 0, this->face ? -28 : -12);
    if (b != NULL) {
        AimVel(&b->base, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI, 32, &b->targetX,
               &b->targetY);
        b->homeX = super->x.HALF.HI;
        b->homeY = GANON_Y(super) + 8; // (a bat's frames stand 8 below its ALttP place)
        b->base.timer = 16;
        EnqueueSFX(SFX_ITEM_LANTERN_ON);
    }
}

/** A blow: counts toward the next phase, or (phase 4) stuns or hurts him. */
static void GanonStruck(AlttpEnemy* this, Entity* by) {
    u32 phase = GanonPhase(this);
    if (this->shield || this->cooldown || super->action == GN_GLIDE) {
        EnqueueSFX(SFX_METAL_CLINK);
        return;
    }
    if (phase == 4) {
        if (super->action == GN_STUN) {
            if (!GanonIsSilverArrow(by)) {
                EnqueueSFX(SFX_METAL_CLINK);
                return;
            }
            this->cooldown = 24;
            EnqueueSFX(SFX_BOSS_HIT);
            if (++this->unused[1] >= GANON_ARROWS) {
                super->health = 0;
                COLLISION_OFF(super);
            }
            return;
        }
        if (super->action != GN_DARK || GanonLitTorches() < 2) {
            return;
        }
        // Ganon_EnableInvincibility: stunned blue, open to the silver arrows
        super->action = GN_STUN;
        super->timer = 127;
        this->cooldown = 12;
        EnqueueSFX(SFX_BOSS_HIT);
        return;
    }
    this->turns++;
    this->cooldown = 24;
    EnqueueSFX(SFX_BOSS_HIT);
    if (phase == 3 && super->action == GN_SPIRAL) {
        // struck mid-spell, he leaps and slams the floor
        super->action = GN_LEAP;
        super->timer = 0;
        this->targetY = 24; // up speed, 1/16 pixels a frame
        this->pose = POSE_CROUCH;
    }
}

static void GanonTick(AlttpEnemy* this) {
    static const u8 poses[] = GANON_POSES;
    u32 phase, t;
    if (super->action == GN_INIT) {
        if (gRoomControls.area == AREA_NULL_3E && CheckLocalFlag(0x7f)) {
            DeleteThisEntity(); // beaten: his hall stays empty (blend.c's sBossRooms)
        }
        AlttpInit(this, AP_GANON, AF_GANON_2_1);
        super->action = GN_INTRO;
        super->timer = 96;
        super->spritePriority.b0 = 3;
        super->spriteSettings.shadow = 2;
        ((Enemy*)this)->enemyFlags |= EM_FLAG_BOSS;
        this->face = 0;
        this->head = 1;
        this->idx = 0;
        this->turns = this->cooldown = this->burst = this->shield = this->crusher = 0;
        this->coerced = 9;
        this->pose = POSE_HOLD;
        this->unused[0] = this->unused[1] = 0;
        this->child = NULL;
        this->step = 1;
        AgahnimFindCenter(this);
        MemClear(&gGanon, sizeof(gGanon));
        gGanon.burn[0] = gGanon.burn[1] = 0xffff;
        {
            // his room's torches, low in the corners of the hall (cells (37, 25) and
            // (57, 25) of room 0x00, about its middle)
            u32 i;
            for (i = 0; i < 2; i++) {
                Entity* tr = GanonSpawnPart(this, GP_TORCH, 0, 0);
                if (tr != NULL) {
                    u32 k;
                    tr->x.HALF.HI = this->homeX + (i ? 80 : -80) * this->rangeX / 88;
                    tr->y.HALF.HI = this->homeY + 80 * this->rangeY / 76;
                    if (gRoomControls.area == AREA_NULL_3E) {
                        // the Pyramid: right on the torches drawn in his hall (8px tiles
                        // (37, 25) and (57, 25))
                        tr->x.HALF.HI = gRoomControls.origin_x + (i ? 464 : 304);
                        tr->y.HALF.HI = gRoomControls.origin_y + 210;
                    }
                    // (in his own room they stand in their places; elsewhere, on the floor)
                    for (k = 0; k < 24 && gRoomControls.area != AREA_NULL_3E && WallAt(tr, 0, 0) && WallAt(tr, 0, -8); k++) {
                        tr->x.HALF.HI += i ? -4 : 4;
                        tr->y.HALF.HI -= 4;
                    }
                    tr->parent = NULL;
                    ((AlttpEnemy*)tr)->idx = i;
                }
            }
        }
        {
            Entity* tri = GanonSpawnPart(this, GP_TRIDENT, 0, 0);
            this->child = tri;
        }
    }
    this->step++;
    if (this->cooldown) {
        this->cooldown--;
    }
    GanonUpdateLight();
    phase = GanonPhase(this);
    t = super->timer;
    this->shield = 0;
    // he turns his head to Link
    {
        s32 dx = gPlayerEntity.base.x.HALF.HI - super->x.HALF.HI;
        this->head = (dx > -32 && dx < 32) ? 1 : (dx < 0 ? (this->face ? 2 : 0) : (this->face ? 0 : 2));
    }
    switch (super->action) {
        case GN_INTRO:
            GanonIdle(this);
            GanonShakeHead(this);
            if (--super->timer == 0) {
                super->action = GN_THROW;
                super->timer = 128;
            }
            break;
        case GN_THROW:
            // spin the trident (128-65), hurl it (64), then warp away (0)
            if (t > 64) {
                GanonSpin(this);
            } else if (t == 64) {
                static const s8 tx[2] = { 24, -16 };
                AlttpEnemy* tri = (AlttpEnemy*)this->child;
                this->coerced = 0;
                if (tri != NULL && ParentAlive(&tri->base)) {
                    static const s8 vx16[16] = { 32, 28, 24, 16, 0, -16, -24, -28, -32, -28, -24, -16, 0, 16, 24, 28 };
                    static const s8 vy16[16] = { 0, 16, 24, 28, 32, 28, 24, 16, 0, -16, -24, -28, -32, -28, -24, -16 };
                    u32 a = (AngleTo(gPlayerEntity.base.x.HALF.HI - super->x.HALF.HI,
                                     gPlayerEntity.base.y.HALF.HI - GANON_Y(super)) + 8) >> 4;
                    // the trident sets off a little to the side of Link, and curves in
                    a = (a + 12 - 2) & 15;
                    // (its frames are about its middle: 1 right of and 4 above its ALttP place)
                    tri->base.x.HALF.HI = super->x.HALF.HI + tx[this->face] + 1;
                    tri->base.y.HALF.HI = GANON_Y(super) + 4 - 4;
                    tri->targetX = vx16[(a + 4) & 15];
                    tri->targetY = vy16[(a + 4) & 15];
                    tri->base.timer = 112;
                    tri->idx = 1;
                    EnqueueSFX(SFX_ITEM_SWORD_BEAM);
                }
                this->pose = this->face ? 10 : 2;
            } else if (t == 0) {
                GanonWarp(this, 5);
                break;
            } else {
                this->pose = this->face ? 10 : 2;
            }
            super->timer--;
            break;
        case GN_WAIT_TRIDENT:
            this->pose = this->face ? 8 : 0;
            this->coerced = 0;
            if (super->timer) {
                super->timer--;
                this->shield = 1;
                if (super->timer & 1) {
                    this->pose = POSE_HIDDEN;
                }
            }
            break;
        case GN_CATCH:
            if (super->timer) {
                super->timer--;
                GanonSpin(this);
            } else {
                super->action = GN_SHAKE;
                super->timer = 127;
                GanonIdle(this);
            }
            break;
        case GN_SHAKE:
            GanonIdle(this);
            GanonShakeHead(this);
            if (--super->timer == 0) {
                if (phase == 1) {
                    super->action = GN_THROW;
                    super->timer = 128;
                } else {
                    super->action = GN_RING;
                    super->timer = 255;
                    gGanon.radius = 40;
                    gGanon.bats = 0;
                }
            }
            break;
        case GN_GLIDE: {
            // a dark streak across the room, flickering, leaving afterimages; he
            // comes to stand where he was going
            s16 vx, vy;
            s32 dx = this->targetX - super->x.HALF.HI, dy = this->targetY - super->y.HALF.HI;
            this->shield = 1;
            this->coerced = 0;
            if (Abs(dx) < 4 && Abs(dy) < 4) {
                super->x.HALF.HI = this->targetX;
                super->y.HALF.HI = this->targetY;
                this->face = this->idx >> 2;
                COLLISION_ON(super);
                if (this->crusher == 5) {
                    super->action = GN_WAIT_TRIDENT;
                    super->timer = 32;
                } else if (phase == 2) {
                    super->action = GN_SHOW;
                    super->timer = 40;
                } else if (phase == 3) {
                    super->action = GN_SHOW3;
                    super->timer = 40;
                } else {
                    super->action = GN_DARK;
                    super->timer = 104;
                }
                break;
            }
            AimVel(super, this->targetX, this->targetY, 32, &vx, &vy);
            MoveVel(super, vx, vy);
            if (super->timer) {
                super->timer--;
            }
            if (super->timer == 0 || (this->step & 1)) {
                this->pose = POSE_HIDDEN;
            } else {
                this->pose = this->face ? 10 : 2;
                if (!(this->step & 7)) {
                    AlttpEnemy* im = (AlttpEnemy*)GanonSpawnPart(this, GP_IMAGE, 0, 0);
                    if (im != NULL) {
                        im->pose = poses[this->pose * 3 + this->head];
                        im->base.timer = 24;
                    }
                }
            }
            break;
        }
        case GN_RING:
            // bats join the ring one by one, as he spins the trident
            gGanon.radius = 40;
            if (t < 0xc0 && (t & 15) == 0 && gGanon.bats < 8) {
                AlttpEnemy* b = (AlttpEnemy*)GanonSpawnPart(this, GP_BAT, 0, 0);
                if (b != NULL) {
                    b->idx = gGanon.bats++;
                    b->base.timer = 48;
                    EnqueueSFX(SFX_ITEM_LANTERN_ON);
                }
            }
            GanonSpin(this);
            GanonRing(this);
            if (--super->timer == 0) {
                super->action = GN_RING_SWIRL;
                super->timer = 255;
            }
            break;
        case GN_RING_SWIRL: {
            // the ring breathes in and out
            static const s8 wobble[16] = { 0, 0, 0, 0, -1, -1, -2, -1, 0, 0, 0, 0, 1, 2, 1, 1 };
            gGanon.radius += wobble[(t >> 4) & 15];
            GanonSpin(this);
            GanonRing(this);
            if (--super->timer == 0) {
                super->action = GN_RING_LET_GO;
                super->timer = 127;
                GanonIdle(this);
                gGanon.radius = 0; // the bats' cue to fly off, each in its turn
            }
            break;
        }
        case GN_RING_LET_GO:
            GanonIdle(this);
            GanonShakeHead(this);
            if (--super->timer == 0) {
                GanonWarp(this, 10);
            }
            break;
        case GN_SHOW:
        case GN_SHOW3:
            // he flickers into sight
            GanonIdle(this);
            this->shield = 1;
            if (super->timer & 1) {
                this->pose = POSE_HIDDEN;
            }
            if (--super->timer == 0) {
                if (super->action == GN_SHOW) {
                    super->action = GN_RING;
                    super->timer = 255;
                    gGanon.bats = 0;
                } else if (Random() & 1) {
                    GanonWarp(this, 13);
                } else {
                    super->action = GN_SPIRAL;
                    super->timer = 127;
                }
            }
            break;
        case GN_SPIRAL: {
            static const u8 gfx[6] = { 5, 6, 7, 13, 14, 10 };
            u32 k = t < 96 ? (t < 72 ? 2 : 1) : 0;
            this->coerced = 0;
            this->pose = gfx[k + (this->face ? 3 : 0)];
            if (t == 66) {
                GanonFling(this, GP_SPIRAL_BAT);
            }
            if (--super->timer == 0) {
                GanonWarp(this, 13);
            }
            break;
        }
        case GN_LEAP: {
            // up (24/16 pixels a frame, slowing), a moment aloft, then down hard
            this->coerced = 0;
            if (super->timer == 0) {
                if (this->targetY > 0) {
                    super->z.WORD -= this->targetY << 12;
                    this->targetY--;
                    this->pose = this->targetY > 18 ? POSE_SPRING : (this->face ? 14 : 6);
                } else {
                    super->timer = 32;
                }
            } else if (--super->timer == 0) {
                super->action = GN_SLAM;
            }
            break;
        }
        case GN_SLAM: {
            if (super->timer == 0) {
                // down at six pixels a frame
                super->z.WORD += 96 << 12;
                this->pose = this->face ? 10 : 2;
                if (super->z.WORD >= 0) {
                    super->z.WORD = 0;
                    super->timer = 96;
                    InitScreenShake(48, 0);
                    EnqueueSFX(SFX_THUD_HEAVY);
                    EnqueueSFX(SFX_ITEM_BOMB_EXPLODE);
                    this->unused[0]++;
                    this->pose = POSE_FALL;
                    {
                        // the floor about him shatters
                        u32 i;
                        for (i = 0; i < 6; i++) {
                            Entity* fx = CreateFx(super, FX_ROCK, 0);
                            if (fx != NULL) {
                                fx->x.HALF.HI += (s32)(Random() & 63) - 32;
                                fx->y.HALF.HI += (s32)(Random() & 31) - 16;
                            }
                        }
                    }
                }
            } else {
                this->pose = super->timer > 80 ? POSE_FALL : (this->face ? 10 : 2);
                if (--super->timer == 0) {
                    if (this->unused[0] >= GANON_SLAMS) {
                        // the last slam: he puts out the lights (Ganon_ExtinguishTorch)
                        gGanon.dark = 1;
                        gGanon.burn[0] = gGanon.burn[1] = 0;
                        EnqueueSFX(SFX_ITEM_LANTERN_OFF);
                        GanonWarp(this, 10);
                    } else {
                        GanonWarp(this, 13);
                    }
                }
            }
            break;
        }
        case GN_DARK: {
            // in the dark: a bat at Link, flickering away in between
            static const u8 gfxA[2] = { 6, 14 };
            static const u8 gfxB[2] = { 7, 10 };
            this->coerced = 0;
            this->pose = gfxA[this->face];
            if (t == 52) {
                GanonFling(this, GP_TRAIL_BAT);
            } else if (t < 52) {
                this->pose = gfxB[this->face];
            }
            if (t >= 72 || t < 40) {
                this->shield = 1;
                if (t & 1) {
                    this->pose = POSE_HIDDEN;
                }
            }
            if (--super->timer == 0) {
                GanonWarp(this, 18);
            }
            break;
        }
        case GN_STUN: {
            static const u8 gfx[2] = { 5, 13 };
            this->coerced = 0;
            this->pose = gfx[this->face];
            if (--super->timer == 0) {
                GanonWarp(this, 18);
            }
            break;
        }
    }
    // in the dark he shows only while a torch burns, and can be struck only with both
    if (gGanon.dark && super->action != GN_STUN) {
        u32 lit = GanonLitTorches();
        if (lit == 0) {
            this->pose = POSE_HIDDEN;
        }
        if (lit < 2) {
            this->shield = 1;
        }
    }
    SetPalette(super, super->action == GN_STUN ? AP_GANON_STUN : AP_GANON);
    if (this->cooldown && (this->cooldown & 2) && this->pose != POSE_HIDDEN) {
        super->spriteSettings.draw = 0;
    } else {
        super->spriteSettings.draw = this->pose != POSE_HIDDEN;
    }
    if (this->pose != POSE_HIDDEN) {
        u32 f;
        if (this->pose == POSE_HOLD) {
            f = AF_GANON_HOLD_0 + this->head;
        } else if (this->pose >= POSE_CROUCH) {
            f = AF_GANON_CROUCH + this->pose - POSE_CROUCH;
        } else {
            f = poses[this->pose * 3 + this->head];
        }
        SetFrame(super, f);
    }
    if (this->shield || super->action == GN_GLIDE || this->pose == POSE_HIDDEN) {
        COLLISION_OFF(super);
    } else {
        COLLISION_ON(super);
    }
    AlttpEndTick(this);
}

static void GanonOnCollision(AlttpEnemy* this);
static void GanonOnDeath(AlttpEnemy* this) {
    if (this->burst == 0) {
        DeleteChildren(super);
        SetPalette(super, AP_GANON_STUN);
        SetFrame(super, AF_GANON_FALL);
        SetHeight(super, 0);
        gGanon.dark = 0;
    }
    GanonUpdateLight();
    if (BossBurst(this, 12, 8)) {
        GanonLightsReset();
        DeleteThisEntity();
    }
}

static void (*const Ganon_Functions[])(AlttpEnemy*) = {
    GanonTick, GanonOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    GanonOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void GanonOnCollision(AlttpEnemy* this) {
    super->knockbackDuration = 0;
    if (TookHit(this)) {
        GanonStruck(this, super->contactedEntity);
    }
    EnemyFunctionHandlerAfterCollision(super, (void (*const*)())Ganon_Functions);
}

void Ganon(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Ganon_Functions);
    BossCam(super, -16);
}

/* ---- his trident, fire bats, flames, torches (GANON_PART) -----------------------------
 * By type: the trident (in his hand, or thrown: it hunts Link for 112 frames, then
 * flies back to his hand), a bat of his ring (it waits its turn, winds up and dashes
 * at Link), a spiralling bat, a bat flung straight at Link, the flames bats leave, a
 * torch of his room, the flame on a lit torch, and his afterimage as he warps. None
 * can be hurt; bats, flames and the thrown trident hurt Link.
 * Fields: targetX/Y the speed (1/16 pixels), homeX/Y a spiral's middle or the thrown
 * trident's place, idx a ring bat's place or a torch's number (or the trident: 1
 * thrown), pose an afterimage's frame, step the clock.
 */
static AlttpEnemy* GanonOf(Entity* e) {
    Entity* g = e->parent;
    if (g != NULL && g->next != NULL && g->kind == ENEMY && g->id == GANON && g->health != 0) {
        return (AlttpEnemy*)g;
    }
    return NULL;
}

static void GanonFlameAt(Entity* from, u32 life) {
    AlttpEnemy* f = (AlttpEnemy*)SpawnAt(GANON_PART, GP_FLAME, from, 0, 8);
    if (f != NULL) {
        f->base.collisionLayer = from->collisionLayer;
        f->base.parent = from->parent;
        f->base.timer = life;
    }
}

static void FireBatAnimate(AlttpEnemy* this) {
    static const u8 gfx[4] = { 4, 5, 6, 5 };
    SetFrame(super, AF_FIREBAT_0 + gfx[(this->step >> 2) & 3]);
}

static void GanonPartTick(AlttpEnemy* this) {
    u32 type = super->type;
    AlttpEnemy* g = GanonOf(super);
    if (super->action == 0) {
        static const u8 pal[8] = { AP_TRIDENT, AP_FIREBAT, AP_FIREBAT, AP_FIREBAT, AP_FIREBAT, AP_GTORCH, AP_FIREBAT, AP_GANON };
        static const u8 frame[8] = { AF_TRIDENT_9, AF_FIREBAT_0, AF_FIREBAT_4, AF_FIREBAT_4, AF_GFLAME_0, AF_GTORCH, AF_GFLAME_0, AF_GANON_2_1 };
        AlttpInit(this, pal[type], frame[type]);
        super->spriteSettings.shadow = 0;
        super->spritePriority.b0 = type == GP_TORCH ? 4 : 2;
        this->step = 0;
        this->child = NULL;
        this->unused[3] = 0;
        if (type == GP_TRIDENT) {
            this->idx = 0;
            this->pose = 0;
        }
        if (type == GP_TORCH || type == GP_TORCH_FIRE || type == GP_IMAGE) {
            COLLISION_OFF(super);
        }
    }
    super->health = 0;
    super->knockbackDuration = 0;
    this->step++;
    switch (type) {
        case GP_TRIDENT: {
            static const u8 spin[8] = { 8, 7, 6, 5, 4, 3, 2, 1 };
            static const s8 atX[5] = { 24, -16, 0, 16, -8 };
            static const s8 atY[5] = { 4, 4, 16, 21, 19 };
            if (g == NULL) {
                DeleteThisEntity();
            }
            if (this->idx == 0) {
                // in his hand (Trident_Draw), or out of sight while he has none
                u32 pose = g->coerced;
                u32 j = pose == 9 ? 3 : pose >= 9 ? 4 : g->face;
                COLLISION_OFF(super);
                super->spriteSettings.draw = pose != 0 && g->base.spriteSettings.draw;
                super->x.HALF.HI = g->base.x.HALF.HI + atX[j] + 1;
                super->y.HALF.HI = GANON_Y(&g->base) + atY[j] - 20;
                super->z.WORD = g->base.z.WORD;
                if (pose) {
                    SetFrame(super, AF_TRIDENT_1 + pose - 1);
                }
                return;
            }
            // thrown: it whirls after Link, then back to his hand
            COLLISION_ON(super);
            super->spriteSettings.draw = 1;
            super->z.WORD = 0;
            if (!(this->step & 15)) {
                EnqueueSFX(SFX_ITEM_GLOVES_AIR);
            }
            MoveVel(super, this->targetX, this->targetY);
            SetFrame(super, AF_TRIDENT_1 + spin[(this->step >> 2) & 7] - 1);
            if (super->timer) {
                super->timer--;
                if (!(super->timer & 1)) {
                    s16 vx, vy;
                    AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI + 4, 32, &vx, &vy);
                    ApproachVel(&this->targetX, vx);
                    ApproachVel(&this->targetY, vy);
                }
            } else {
                s32 hx = g->base.x.HALF.HI + (g->face ? -16 : 24) + 1;
                s32 hy = GANON_Y(&g->base) - 16 - 4;
                s16 vx, vy;
                if (Abs(super->x.HALF.HI - hx) < 5 && Abs(super->y.HALF.HI - hy) < 5 &&
                    g->base.action == GN_WAIT_TRIDENT) {
                    // caught: he spins it again
                    this->idx = 0;
                    g->base.action = GN_CATCH;
                    g->base.timer = 16;
                    g->coerced = 1;
                    return;
                }
                AimVel(super, hx, hy, 32, &vx, &vy);
                ApproachVel(&this->targetX, vx);
                ApproachVel(&this->targetY, vy);
                ApproachVel(&this->targetX, vx);
                ApproachVel(&this->targetY, vy);
            }
            return;
        }
        case GP_BAT: {
            // Sprite_FireBat_Launched: in the ring until its turn, then a wind-up and a dash
            static const u8 letGo[8] = { 0x10, 0x30, 0x50, 0x70, 0x90, 0xb0, 0xd0, 0xbd };
            if (this->idx < 0x80) {
                if (g == NULL) {
                    DeleteThisEntity();
                }
                if (gGanon.radius != 0) {
                    static const s8 bx[2] = { 20, -18 };
                    u32 t = (gGanon.angle + this->idx * 64) & 0x1ff;
                    s32 cx = g->base.x.HALF.HI + bx[g->face];
                    s32 cy = GANON_Y(&g->base) - 20;
                    s32 nx = cx + HalfSin(t, gGanon.radius);
                    s32 ny = cy + HalfSin(t + 0x80, gGanon.radius) + 8;
                    // its speed along the ring, kept for when it lets go
                    this->targetX = (nx - super->x.HALF.HI) << 4;
                    this->targetY = (ny - super->y.HALF.HI) << 4;
                    this->targetX = this->targetX > 8 ? 8 : this->targetX < -8 ? -8 : this->targetX;
                    this->targetY = this->targetY > 8 ? 8 : this->targetY < -8 ? -8 : this->targetY;
                    super->x.HALF.HI = nx;
                    super->y.HALF.HI = ny;
                    if (super->timer) {
                        super->timer--;
                        SetFrame(super, AF_FIREBAT_0 + ((super->timer >> 2) & 1));
                    } else {
                        SetFrame(super, AF_FIREBAT_0 + ((this->step >> 2) & 1));
                    }
                    COLLISION_ON(super);
                    return;
                }
                // let go: each waits its turn (kGanon_Delay8), drifting
                super->timer = letGo[this->idx & 7];
                this->idx = 0x80;
                this->unused[0] = 0;
            }
            MoveVel(super, this->targetX, this->targetY);
            if (super->timer) {
                if (--super->timer == 0) {
                    this->unused[0] = 35;
                }
                SetFrame(super, AF_FIREBAT_0 + ((super->timer >> 2) & 1));
            } else if (this->unused[0] > 1) {
                static const u8 wind[9] = { 4, 4, 4, 3, 3, 3, 2, 2, 2 };
                this->unused[0]--;
                SetFrame(super, AF_FIREBAT_0 + wind[this->unused[0] >> 2]);
            } else {
                if (this->unused[0] == 1) {
                    this->unused[0] = 0;
                    AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI, 48, &this->targetX,
                           &this->targetY);
                    EnqueueSFX(SFX_ITEM_LANTERN_ON);
                }
                this->step++;
                FireBatAnimate(this);
            }
            break;
        }
        case GP_SPIRAL_BAT: {
            // Sprite_SpiralFireBat: about the place he flung it from, wider and wider
            s16 ix, iy, tx, ty;
            if (super->timer) {
                super->timer--;
            } else {
                AimVel(super, this->homeX, this->homeY, 2, &ix, &iy);
                AimVel(super, this->homeX, this->homeY, 80, &tx, &ty);
                this->targetX = ty - ix;
                this->targetY = -tx - iy;
            }
            MoveVel(super, this->targetX, this->targetY);
            FireBatAnimate(this);
            if (!(this->step & 7)) {
                GanonFlameAt(super, 0x4f);
            }
            break;
        }
        case GP_TRAIL_BAT:
            MoveVel(super, this->targetX, this->targetY);
            FireBatAnimate(this);
            if (!(this->step & 7)) {
                GanonFlameAt(super, 0x2f);
            }
            break;
        case GP_FLAME: {
            // Garnish10_GanonBatFlame: it burns down, flickering, then withers
            static const u8 idx[16] = { 3, 2, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 };
            if (super->timer == 0 || --super->timer == 0) {
                DeleteThisEntity();
            }
            if (super->timer < 8) {
                COLLISION_OFF(super);
                SetFrame(super, AF_GFLAME_WITHER);
                super->y.HALF.HI -= this->step & 1;
            } else {
                SetFrame(super, AF_GFLAME_0 + idx[super->timer >> 3]);
            }
            return;
        }
        case GP_TORCH: {
            // lit by the Fire Rod's shot, or the lantern held to it; it burns 512 frames
            u32 i = this->idx & 1;
            u32 k;
            if (gGanon.burn[i] != 0 && gGanon.burn[i] != 0xffff && --gGanon.burn[i] == 0) {
                EnqueueSFX(SFX_ITEM_LANTERN_OFF);
            }
            if (gGanon.burn[i] == 0) {
                bool32 light = (gPlayerState.flags & PL_USE_LANTERN) &&
                               Abs(gPlayerEntity.base.x.HALF.HI - super->x.HALF.HI) < 20 &&
                               Abs(gPlayerEntity.base.y.HALF.HI - super->y.HALF.HI) < 24;
                for (k = 0; k < ARRAY_COUNT(gEntityLists) && !light; k++) {
                    LinkedList* list = &gEntityLists[k];
                    Entity* e;
                    for (e = list->first; (u32)e != (u32)list; e = e->next) {
                        // (a shot bursts on the stone block the torch stands in, short of its bowl)
                        if (IsRodShot(e, ITEM_FIRE_ROD) && Abs(e->x.HALF.HI - super->x.HALF.HI) < 16 &&
                            Abs(e->y.HALF.HI - super->y.HALF.HI + 4) < 24) {
                            light = TRUE;
                            break;
                        }
                    }
                }
                if (light) {
                    gGanon.burn[i] = 512;
                    EnqueueSFX(SFX_ITEM_LANTERN_ON);
                }
            }
            if (gGanon.burn[i] != 0 && this->child == NULL) {
                Entity* f = SpawnAt(GANON_PART, GP_TORCH_FIRE, super, -7, -10); // (centered on the bowl)
                if (f != NULL) {
                    f->collisionLayer = super->collisionLayer;
                    this->child = f;
                }
            }
            SetFrame(super, AF_GTORCH);
            return;
        }
        case GP_TORCH_FIRE: {
            Entity* torch = super->parent;
            if (torch == NULL || torch->next == NULL || gGanon.burn[((AlttpEnemy*)torch)->idx & 1] == 0) {
                if (torch != NULL && torch->next != NULL) {
                    ((AlttpEnemy*)torch)->child = NULL;
                }
                DeleteThisEntity();
            }
            SetFrame(super, AF_GFLAME_0 + ((this->step >> 3) & 1));
            return;
        }
        case GP_IMAGE:
            COLLISION_OFF(super);
            SetFrame(super, this->pose);
            super->spriteSettings.draw = (super->timer & 1);
            if (super->timer == 0 || --super->timer == 0) {
                DeleteThisEntity();
            }
            return;
    }
    // bats go when they leave the room, or after a while
    if (!(this->step & 3) && ++this->unused[3] > 120) {
        DeleteThisEntity();
    }
    if (!InRoom(super, 16)) {
        DeleteThisEntity();
    }
}

void GanonPart(AlttpEnemy* this) {
    GanonPartTick(this);
}

/* =============================================================================
 * Ganon's Tower's last ones (sprite 340): the soldiers, the wall cannons and their
 * cannonballs, the flying floor tiles, and the Sparks that run along the walls.
 * ========================================================================== */

static const s8 sFaceX[4] = { 1, -1, 0, 0 };
static const s8 sFaceY[4] = { 0, 0, 1, -1 };

/** Any of the player's items of this id within range (the Boomerang, an arrow). */
static bool32 PlayerItemNear(Entity* at, u32 id, s32 range) {
    u32 i;
    for (i = 0; i < ARRAY_COUNT(gEntityLists); i++) {
        LinkedList* list = &gEntityLists[i];
        Entity* e;
        for (e = list->first; (u32)e != (u32)list; e = e->next) {
            if (((e->kind == PLAYER_ITEM && e->id == id) || (id == 0xff && IsHook(e))) &&
                Abs(e->x.HALF.HI - at->x.HALF.HI) < range && Abs(e->y.HALF.HI - at->y.HALF.HI) < range) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* ---- soldiers (sprites 0x41 and 0x43) --------------------------------------------
 * Type 0 the blue sword soldier, 1 the red spearman (in the Dark World, the pig-faced
 * soldiers of the room's own sprite set). As Guard_Main: it walks a while one way
 * (turning back at walls), stops and looks about, its head glancing left and right,
 * and walks on. All the while it watches the half of the room before it (zelda3's
 * probes): seeing Link, or touched or struck by him, it starts, turns to him and
 * gives chase, steering round walls, for about four seconds. The spearman, near Link
 * and facing him, thrusts its spear. Fields: face the body, head the head, turns a
 * glance clock, pose a stagger after a blow, cooldown the spear.
 */
enum { SD_INIT, SD_WAIT, SD_WALK, SD_LOOK, SD_ALERT, SD_CHASE, SD_THRUST };

static const u8 sSoldierWalk[4][4] = { { 11, 12, 13, 12 }, { 4, 5, 6, 5 }, { 0, 1, 2, 3 }, { 7, 8, 9, 10 } };
/* kSoldier_HeadDirs: where the head looks while it stands, by facing and clock */
static const u8 sSoldierGlance[32] = { 0, 2, 2, 2, 0, 3, 3, 3, 1, 3, 3, 3, 1, 2, 2, 2,
                                       2, 0, 0, 0, 2, 1, 1, 1, 3, 1, 1, 1, 3, 0, 0, 0 };
/* the spear thrust out: the body's box reaching to the spear's head */
static const Hitbox sSpearHitbox[4] = {
    { 10, -6, { 6, 4, 4, 6 }, 14, 7 },
    { -10, -6, { 6, 4, 4, 6 }, 14, 7 },
    { 0, 4, { 6, 4, 4, 6 }, 6, 17 },
    { 0, -16, { 6, 4, 4, 6 }, 6, 17 },
};

/** Link in the half of the room the soldier looks into, nothing solid between. */
static bool32 SoldierSeesLink(AlttpEnemy* this) {
    Entity* link = &gPlayerEntity.base;
    s32 dx = link->x.HALF.HI - super->x.HALF.HI;
    s32 dy = link->y.HALF.HI - super->y.HALF.HI;
    s32 d = this->head;
    s32 ahead = dx * sFaceX[d] + dy * sFaceY[d];
    s32 n, i;
    if (ahead < 0 || Distance(dx, dy) > 160 || link->z.HALF.HI < -16 || gPlayerState.framestate == PL_STATE_DIE) {
        return FALSE;
    }
    n = Distance(dx, dy) >> 3;
    for (i = 1; i < n; i++) {
        if (WallAt(super, dx * i / n, dy * i / n - 4)) {
            return FALSE;
        }
    }
    return TRUE;
}

static void SoldierAlert(AlttpEnemy* this) {
    if (super->action < SD_ALERT) {
        super->action = SD_ALERT;
        super->timer = 32;
        this->face = this->head = Face4ToLink(super);
        EnqueueSFX(SFX_EM_ARMOS_ON);
    }
}

/** A wall or a pit (act tile 0x19, ACT_TILE_25: a hole) this far off. */
static bool32 SoldierBlocked(AlttpEnemy* this, s32 dx, s32 dy) {
    return WallAt(super, dx, dy) ||
           GetActTileAtWorldCoords(super->x.HALF.HI + dx, super->y.HALF.HI + dy, super->collisionLayer) == 0x19;
}

/** Minish Cap's movement, kept out of the pits too (Minish Cap's walkers go over
 * them): a step onto one is taken back and counts as running into a wall. */
static void SoldierMove(AlttpEnemy* this) {
    s32 x = super->x.WORD, y = super->y.WORD;
    // (knocked onto an edge already, it may walk off it)
    bool32 free = !SoldierBlocked(this, 0, 0);
    ProcessMovement0(super);
    if (!free) {
        return;
    }
    if (super->x.WORD != x && SoldierBlocked(this, super->x.WORD > x ? 6 : -6, 0)) {
        super->x.WORD = x;
        super->collisions |= COL_EAST_ANY;
    }
    if (super->y.WORD != y && SoldierBlocked(this, 0, super->y.WORD > y ? 6 : -6)) {
        super->y.WORD = y;
        super->collisions |= COL_SOUTH_ANY;
    }
}

static void SoldierStartChase(AlttpEnemy* this, u32 time) {
    super->action = SD_CHASE;
    super->timer = time;
    this->step = 31;
}

static void SoldierTick(AlttpEnemy* this) {
    u32 base = super->type ? AF_SPEARMAN_0 - AF_SOLDIER_0 : 0;
    u32 f, frame;
    if (super->action == SD_INIT) {
        AlttpInit(this, super->type ? AP_SPEARMAN : AP_SOLDIER, AF_SOLDIER_0 + base);
        super->action = SD_WAIT;
        super->timer = 16 + (Random() & 31);
        this->face = this->head = Random() & 3;
        this->targetX = this->targetY = 0;
        this->pose = 0;
        this->cooldown = 0;
    }
    this->step++;
    if (this->pose) {
        this->pose--;
    }
    if (this->cooldown) {
        this->cooldown--;
    }
    super->hitbox = (Hitbox*)&gHitbox_AlttpTall;
    if (super->action < SD_ALERT && ((!(this->step & 3) && SoldierSeesLink(this)) || LinkNear(super, 14, 14))) {
        SoldierAlert(this);
    }
    switch (super->action) {
        case SD_WAIT: // a moment's rest, then a way to walk
            if (--super->timer == 0) {
                super->action = SD_WALK;
                super->timer = (Random() & 0x3f) + 0x28;
                this->face = this->head = Random() & 3;
            }
            break;
        case SD_WALK:
            super->direction = sFaceToDir[this->face];
            super->speed = 0x80;
            SoldierMove(this);
            if (super->collisions != COL_NONE) {
                this->face ^= 1;    // back the other way
                this->head = this->face;
            }
            // (zelda3 holds its clock back every other frame)
            if ((this->step & 1) && --super->timer == 0) {
                super->action = SD_LOOK;
                super->timer = 160;
            }
            break;
        case SD_LOOK: // stands and looks about
            if (super->timer < 128) {
                this->head = sSoldierGlance[this->face * 8 + ((super->timer >> 3) & 7)];
            }
            if (--super->timer == 0) {
                this->head = this->face;
                super->action = SD_WAIT;
                super->timer = 32;
            }
            break;
        case SD_ALERT: // turned to Link, a beat before it runs at him
            this->face = this->head = Face4ToLink(super);
            if (--super->timer == 0) {
                SoldierStartChase(this, 255);
            }
            break;
        case SD_CHASE: {
            s32 dx = gPlayerEntity.base.x.HALF.HI - super->x.HALF.HI;
            s32 dy = gPlayerEntity.base.y.HALF.HI - super->y.HALF.HI;
            if (!(this->step & 31)) {
                // Sprite_ApplySpeedTowardsLink(16), the body turned to him
                super->direction = GetFacingDirection(super, &gPlayerEntity.base);
                super->speed = 0x100;
                this->face = this->head = Face4ToLink(super);
            }
            SoldierMove(this);
            // Guard_ApplySpeedInDirection: blocked, it slides along the wall toward him
            if (super->collisions & (COL_EAST_ANY | COL_WEST_ANY)) {
                super->direction = dy >= 0 ? DirectionSouth : DirectionNorth;
                super->speed = 0xe0;
            } else if (super->collisions & (COL_NORTH_ANY | COL_SOUTH_ANY)) {
                super->direction = dx >= 0 ? DirectionEast : DirectionWest;
                super->speed = 0xe0;
            }
            if (super->type == 1 && this->cooldown == 0) {
                s32 ahead = dx * sFaceX[this->face] + dy * sFaceY[this->face];
                s32 side = Abs(dx * sFaceY[this->face] + dy * sFaceX[this->face]);
                if (ahead > 0 && ahead < 40 && side < 12) {
                    super->action = SD_THRUST;
                    super->timer = 30;
                    break;
                }
            }
            if (--super->timer == 0) {
                this->face = this->head = Face4ToLink(super);
                super->action = SD_LOOK;
                super->timer = 160;
            }
            break;
        }
        case SD_THRUST: // draws the spear back, lunges, holds it out
            if (super->timer <= 22 && super->timer > 10) {
                super->hitbox = (Hitbox*)&sSpearHitbox[this->face];
                super->direction = sFaceToDir[this->face];
                super->speed = 0x180;
                SoldierMove(this);
            } else if (super->timer <= 10) {
                super->hitbox = (Hitbox*)&sSpearHitbox[this->face];
            }
            if (super->timer == 22) {
                EnqueueSFX(SFX_EM_MOBLIN_SPEAR);
            }
            if (--super->timer == 0) {
                this->cooldown = 48;
                SoldierStartChase(this, 96);
            }
            break;
    }
    f = this->face;
    if (this->pose) {
        frame = AF_SOLDIER_HURT_0 + f * 3;
    } else if (super->action == SD_ALERT && super->timer > 20) {
        frame = AF_SOLDIER_ALERT_0 + f * 3;
    } else if (super->action == SD_THRUST && super->timer <= 22) {
        frame = AF_SOLDIER_THRUST_0 + f * 3;
    } else if (super->action == SD_THRUST) {
        frame = AF_SOLDIER_0 + sSoldierWalk[f][0];
    } else if (super->action == SD_WALK) {
        frame = AF_SOLDIER_0 + sSoldierWalk[f][(this->step >> 3) & 3];
    } else if (super->action == SD_CHASE) {
        frame = AF_SOLDIER_0 + sSoldierWalk[f][(this->step >> 2) & 3];
    } else if (this->head != f) {
        frame = AF_SOLDIER_LOOK_0_1 + f * 3 + (this->head > f ? this->head - 1 : this->head);
    } else {
        frame = AF_SOLDIER_0 + sSoldierWalk[f][0];
    }
    SetFrame(super, frame + base);
    AlttpEndTick(this);
}

static void SoldierOnCollision(AlttpEnemy* this);
static void SoldierOnDeath(AlttpEnemy* this) {
    AlttpDeath(this, SPEAR_MOBLIN);
}

static void (*const Soldier_Functions[])(AlttpEnemy*) = {
    SoldierTick, SoldierOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    SoldierOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void SoldierOnCollision(AlttpEnemy* this) {
    if (super->health < this->savedHealth) {
        // struck: it staggers, then goes for Link (zelda3's sprite_F)
        this->pose = 12;
        this->face = this->head = Face4ToLink(super);
        if (super->action != SD_THRUST) {
            SoldierStartChase(this, 255);
        }
    } else if (super->action < SD_ALERT) {
        SoldierAlert(this);
    }
    AlttpOnCollision(this, (void (*const*)())Soldier_Functions);
}

void AlttpSoldier(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)Soldier_Functions);
    EnemySetFXOffset(super, 0, 1, -20);
}

/* ---- wall cannons (sprites 0x66-0x69) and their cannonballs --------------------------
 * Types 0-3: a cannon set in the wall, firing right, left, down or up. As
 * Sprite_66_WallCannonVerticalLeft: it slides along its wall, 128 pixels one way and
 * back, and every half second kicks back and fires a cannonball. Type 4: the ball,
 * rolling straight across the room (out of the wall for its first 32 frames) and
 * smashing on the far wall. Neither can be hurt; they never hold doors shut.
 */
static void CannonballTick(AlttpEnemy* this) {
    if (super->action == 0) {
        AlttpInit(this, AP_CANNONBALL, AF_CANNONBALL_0);
        super->timer = 32;
        super->spritePriority.b0 = 2;
        this->pose = 0;
    }
    super->health = 0;
    super->knockbackDuration = 0;
    this->step++;
    if (this->pose) {
        // smashed on the wall
        COLLISION_OFF(super);
        SetFrame(super, AF_CANNONBALL_SMASH);
        if (--this->pose == 0) {
            DeleteThisEntity();
        }
        return;
    }
    MoveVel(super, this->targetX, this->targetY);
    if (super->timer) {
        super->timer--;
    } else if (ShotWallAt(super) || !InRoom(super, 0)) {
        this->pose = 6;
        CreateFx(super, FX_ROCK_SMALL, 0);
        EnqueueSFX(SFX_PLACE_OBJ);
        return;
    }
    SetFrame(super, AF_CANNONBALL_0 + ((this->step >> 2) & 3));
}

void WallCannon(AlttpEnemy* this) {
    static const s8 kickX[4] = { 8, -8, 0, 0 };
    static const s8 kickY[4] = { 0, 0, 8, -8 };
    u32 d = super->type;
    if (d == 4) {
        CannonballTick(this);
        return;
    }
    if (super->action == 0) {
        AlttpInit(this, AP_CANNON, AF_CANNON_0_0 + d * 3);
        super->spriteSettings.shadow = 0;
        super->spritePriority.b0 = 3;
        // SpritePrep_WallCannon: one firing across slides up and down, one firing
        // down or up slides across; the first leg goes down or right
        this->idx = (d & 2) ^ 1;
        super->timer = 128;
        this->step = Random() & 31;
        this->pose = 0;
    }
    super->health = 0;
    super->knockbackDuration = 0;
    this->step++;
    if (--super->timer == 0) {
        super->timer = 128;
        this->idx ^= 1;
    }
    {
        static const s8 slideX[4] = { 0, 0, -1, 1 };
        static const s8 slideY[4] = { -1, 1, 0, 0 };
        super->x.HALF.HI += slideX[this->idx];
        super->y.HALF.HI += slideY[this->idx];
    }
    if (!(this->step & 31)) {
        this->pose = 16;
    }
    if (this->pose && --this->pose == 1) {
        AlttpEnemy* b = (AlttpEnemy*)SpawnAt(WALL_CANNON, 4, super, kickX[d], kickY[d]);
        if (b != NULL) {
            b->targetX = kickX[d] * 3;   // 24: a pixel and a half a frame
            b->targetY = kickY[d] * 3;
            // the cannon sits in the wall: the ball rolls on the floor Link stands on
            b->base.collisionLayer = gPlayerEntity.base.collisionLayer;
        }
        if (InRoom(&gPlayerEntity.base, 0)) {
            EnqueueSFX(SFX_THUD_HEAVY);
        }
    }
    SetFrame(super, this->pose > 12 ? AF_CANNON_KICK_0 + d * 3 : AF_CANNON_0_0 + d * 3 + (this->pose != 0));
}

/* ---- flying floor tiles (overlord 0x14, sprite 0x94) --------------------------------
 * Type 0: the tile room (Overlord14_TileRoom), invisible. Once Link is in its quarter
 * of the room it lifts 22 tiles out of the floor, one every second and a half, at
 * zelda3's places. Type 1: a tile. It tips up out of the floor and rises, spinning,
 * hangs a moment, then flies at Link (aiming at him as it goes) and breaks on him, on a
 * wall, or under a blow.
 */
static void FlyingTileBreak(AlttpEnemy* this) {
    super->action = 3;
    super->timer = 8;
    COLLISION_OFF(super);
    super->spriteSettings.shadow = 0;
    EnqueueSFX(SFX_PLACE_OBJ);
    CreateFx(super, FX_ROCK_SMALL, 0);
}

static void FlyingTileTick(AlttpEnemy* this) {
    static const u8 placeX[22] = { 0x70, 0x80, 0x60, 0x90, 0x90, 0x60, 0x70, 0x80, 0x80, 0x70, 0x50,
                                   0xa0, 0xa0, 0x50, 0x50, 0xa0, 0xa0, 0x50, 0x70, 0x80, 0x80, 0x70 };
    static const u8 placeY[22] = { 0x80, 0x80, 0x70, 0x90, 0x70, 0x90, 0x60, 0xa0, 0x60, 0xa0, 0x60,
                                   0xb0, 0x60, 0xb0, 0x80, 0x90, 0x80, 0x90, 0x70, 0x90, 0x70, 0x90 };
    if (super->type == 0) {
        // the tile room
        s32 qx = (super->x.HALF.HI - gRoomControls.origin_x) & ~0xff;
        s32 qy = (super->y.HALF.HI - gRoomControls.origin_y) & ~0xff;
        s32 lx = gPlayerEntity.base.x.HALF.HI - gRoomControls.origin_x - qx;
        s32 ly = gPlayerEntity.base.y.HALF.HI - gRoomControls.origin_y - qy;
        if (super->action == 0) {
            super->action = 1;
            super->spriteSettings.draw = 0;
            COLLISION_OFF(super);
            super->timer = 128;
            this->idx = 0;
        }
        super->health = 0;
        if (lx < 0 || ly < 0 || lx >= 256 || ly >= 256) {
            return;
        }
        if (--super->timer == 0) {
            AlttpEnemy* t = (AlttpEnemy*)CreateEnemy(FLYING_TILE, 1);
            super->timer = 96;
            if (t != NULL) {
                t->base.x.HALF.HI = gRoomControls.origin_x + qx + placeX[this->idx] + 8;
                t->base.y.HALF.HI = gRoomControls.origin_y + qy + placeY[this->idx];
                t->base.collisionLayer = gPlayerEntity.base.collisionLayer;
                if (++this->idx == 22) {
                    DeleteThisEntity();
                }
            } else {
                super->timer = 1;   // no room for it yet: try again next frame
            }
        }
        return;
    }
    if (super->action == 0) {
        AlttpInit(this, AP_FTILE, AF_FTILE_TIP_0);
        super->action = 1;
        super->timer = 128;
        super->spritePriority.b0 = 2;
        this->homeX = 0;    // height, in 1/16 pixels
        COLLISION_OFF(super);
    }
    this->step++;
    switch (super->action) {
        case 1: // rising
            if (super->timer >= 64) {
                this->homeX += 4;
            }
            if (!(this->step & 15)) {
                EnqueueSFX(SFX_LAVA_TILE_WOBBLE);
            }
            if (--super->timer == 0) {
                // off it goes at Link's height (Minish Cap's collision keeps to the floor)
                super->action = 2;
                super->timer = 16;
                super->y.HALF.HI -= this->homeX >> 4;
                this->homeX = 0;
                super->spriteSettings.shadow = 0;
                COLLISION_ON(super);
                AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI - 4, 32, &this->targetX,
                       &this->targetY);
                EnqueueSFX(SFX_LAVA_TILE_FLIP);
            }
            break;
        case 2: // flying at Link
            if (super->timer) {
                super->timer--;
                if (!(super->timer & 3)) {
                    AimVel(super, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI - 4, 32, &this->targetX,
                           &this->targetY);
                }
            }
            MoveVel(super, this->targetX, this->targetY);
            // it breaks on Link too while he is still flashing from a blow (no collision then)
            if (ShotWallAt(super) || !InRoom(super, 0) ||
                (Abs(super->x.HALF.HI - gPlayerEntity.base.x.HALF.HI) < 8 &&
                 Abs(super->y.HALF.HI - gPlayerEntity.base.y.HALF.HI + 4) < 8)) {
                FlyingTileBreak(this);
            }
            break;
        case 3: // in pieces
            if (--super->timer == 0) {
                DeleteThisEntity();
            }
            break;
    }
    SetHeight(super, this->homeX >> 4);
    if (super->action == 3) {
        SetFrame(super, AF_FTILE_BREAK);
    } else if (super->action == 1 && super->timer > 112) {
        SetFrame(super, super->timer > 120 ? AF_FTILE_TIP_0 : AF_FTILE_TIP_1);
    } else {
        SetFrame(super, AF_FTILE_0 + ((this->step >> 2) & 1));
    }
    AlttpEndTick(this);
}

static void FlyingTileOnCollision(AlttpEnemy* this);
static void FlyingTileOnDeath(AlttpEnemy* this) {
    if (super->type == 0) {
        DeleteThisEntity();
    }
    CreateFx(super, FX_ROCK_SMALL, 0);
    EnqueueSFX(SFX_PLACE_OBJ);
    DeleteThisEntity();
}

static void (*const FlyingTile_Functions[])(AlttpEnemy*) = {
    FlyingTileTick, FlyingTileOnCollision, (void (*)(AlttpEnemy*))GenericKnockback,
    FlyingTileOnDeath, (void (*)(AlttpEnemy*))GenericConfused, AlttpGrabbed,
};

static void FlyingTileOnCollision(AlttpEnemy* this) {
    // whatever it meets, it breaks on
    super->knockbackDuration = 0;
    if (super->action == 2) {
        FlyingTileBreak(this);
    }
    EnemyFunctionHandlerAfterCollision(super, (void (*const*)())FlyingTile_Functions);
}

void FlyingTile(AlttpEnemy* this) {
    if (super->type == 0) {
        FlyingTileTick(this);
        return;
    }
    EnemyFunctionHandler(super, (EntityActionArray)FlyingTile_Functions);
}

/* ---- Sparks (sprites 0x5B and 0x5C) --------------------------------------------------
 * A crackling ball that runs along the walls: type 0 (0x5B) keeps the wall on its left
 * and goes round a room clockwise, type 1 (0x5C) the other way. It finds its wall
 * where it starts (or the first one it meets), turns along it at inner corners, and
 * round outer ones once it is past them. It spins and flickers as it goes, shocks
 * Link on touch and turns his sword; the Boomerang or the Hookshot puts it out (and
 * it leaves a fairy, as Minish Cap's Sparks do). It never holds doors shut.
 * Fields: face its way (0 right, 1 left, 2 down, 3 up), turns the frames before it may
 * round an outer corner, pose the frames to go before it does, idx 1 once it has a wall.
 */
static bool32 SparkWall(AlttpEnemy* this, u32 face, s32 reach) {
    return WallAt(super, sFaceX[face] * reach, sFaceY[face] * reach);
}

void AlttpSpark(AlttpEnemy* this) {
    // the side its wall is on, and the way it turns round an inner corner, by facing:
    // to the left (clockwise) or right (counterclockwise)
    static const u8 leftOf[4] = { 3, 2, 0, 1 };
    static const u8 rightOf[4] = { 2, 3, 1, 0 };
    const u8* wallSide = super->type ? rightOf : leftOf;
    const u8* away = super->type ? leftOf : rightOf;
    u32 i;
    if (super->action == 0) {
        AlttpInit(this, AP_SPARK, AF_SPARK_0);
        super->spriteSettings.shadow = 0;
        // started on a pit's edge or in a wall, the room gave it the upper layer: it runs
        // on the floor Link stands on
        super->collisionLayer = gPlayerEntity.base.collisionLayer;
        super->spritePriority.b0 = 2;
        this->idx = 0;
        this->face = super->type ? 2 : 3;
        // set into a wall's edge in A Link to the Past (its walls are finer than the
        // blend's): out onto the nearest floor
        if (WallAt(super, 0, 0)) {
            s32 r;
            for (r = 8; r <= 32 && WallAt(super, 0, 0); r += 8) {
                for (i = 0; i < 4; i++) {
                    if (!WallAt(super, sFaceX[i] * r, sFaceY[i] * r)) {
                        super->x.HALF.HI += sFaceX[i] * r;
                        super->y.HALF.HI += sFaceY[i] * r;
                        break;
                    }
                }
            }
        }
        // a wall beside it: run along it (zelda3 tests its four sides)
        for (i = 0; i < 4; i++) {
            if (SparkWall(this, wallSide[i], 9)) {
                this->face = i;
                this->idx = 1;
                break;
            }
        }
        this->turns = 0;
        this->pose = 0;
    }
    super->health = 0;
    super->knockbackDuration = 0;
    this->step++;
    if (super->action == 2) {
        // put out
        COLLISION_OFF(super);
        SetFrame(super, AF_SPARK_FIZZLE);
        if (--super->timer == 0) {
            Entity* fairy = CreateObjectWithParent(super, GROUND_ITEM, 0x60, 0);
            if (fairy != NULL) {
                fairy->y.HALF.HI -= 4;
            }
            CreateFx(super, FX_DEATH, 0);
            DeleteThisEntity();
        }
        return;
    }
    if (PlayerItemNear(super, PLAYER_ITEM_BOOMERANG, 12) || PlayerItemNear(super, 0xff, 12)) {
        super->action = 2;
        super->timer = 12;
        EnqueueSFX(SFX_EVAPORATE);
        return;
    }
    // zelda3's speed 16: a pixel a frame
    for (i = 0; i < 1; i++) {
        if (this->pose) {
            // past an outer corner: round it
            if (--this->pose == 0) {
                this->face = wallSide[this->face];
                this->turns = 12;
            }
        } else if (this->idx && !this->turns && !SparkWall(this, wallSide[this->face], 9)) {
            this->pose = 8;
        }
        if (this->turns) {
            this->turns--;
        }
        if (SparkWall(this, this->face, 9)) {
            // an inner corner (or the first wall it meets): turn away along it
            this->face = away[this->face];
            this->idx = 1;
            this->pose = 0;
            if (SparkWall(this, this->face, 9)) {
                this->face ^= 1; // a dead end
            }
            continue;
        }
        super->x.HALF.HI += sFaceX[this->face];
        super->y.HALF.HI += sFaceY[this->face];
    }
    SetFrame(super, AF_SPARK_0 + ((this->step & 1) << 2) + ((this->step >> 2) & 3));
}
