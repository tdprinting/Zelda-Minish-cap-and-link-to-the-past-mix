/**
 * @file alttpEnemies.c
 * @ingroup Enemies
 *
 * @brief A Link to the Past's enemies, brought into the blend with their own
 * behavior: Stalfos, Eyegores, Popos, Antifairies and the Armos Knights.
 *
 * Their graphics are A Link to the Past's sprite sheets (plus the frames Minish Cap's
 * motion adds), made at build time from the player's cartridge by
 * blend/enemies/alttp_enemies.py into sprite 155. Every frame is a one-frame
 * animation, and these enemies pick their frames themselves.
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
