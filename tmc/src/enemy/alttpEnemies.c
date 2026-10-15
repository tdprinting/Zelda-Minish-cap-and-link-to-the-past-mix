/**
 * @file alttpEnemies.c
 * @ingroup Enemies
 *
 * @brief A Link to the Past's enemies, brought into the blend with their own
 * behavior. The Eastern Palace: Stalfos, Eyegores, Popos, Antifairies and the Armos
 * Knights. The Dark World: Ropes, Moblins and Hinox. The Desert Palace: Leevers,
 * Beamos, Pokeys, Gibdos and the Lanmolas. The Tower of Hera: Baris, Hardhat
 * Beetles, Bumpers and Moldorm.
 *
 * Their graphics are A Link to the Past's sprite sheets (plus the frames Minish Cap's
 * motion adds), made at build time from the player's cartridge by
 * blend/enemies/alttp_enemies.py into sprites 155 and 329. Every frame is a
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
#include "../../custom/gen/alttp_enemies.h"

extern const u16 gAlttpEnemyPalettes[];
extern u8 gAlttpEnemyState[];
u32 FindFreeObjPalette(u32 paletteCount);
void SetEntityObjPalette(Entity* entity, s32 palette);
void CleanUpObjPalettes(void);

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
    SetPalette(super, pal);
    super->animIndex = 0xff;
    SetFrame(super, frame);
    this->savedHealth = super->health;
    super->action = 1;
}

/** A hit that only clinks off (shield up): put the health back and skip the knockback. */
static void BlockHit(AlttpEnemy* this) {
    if (super->health < this->savedHealth) {
        super->health = this->savedHealth;
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
        super->health = this->savedHealth;
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
        super->health = this->savedHealth;
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
