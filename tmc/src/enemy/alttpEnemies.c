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
 * spike blocks, Medusa statues, and the Helmasaur King, Arrghus and Mothula.
 *
 * Their graphics are A Link to the Past's sprite sheets (plus the frames Minish Cap's
 * motion adds), made at build time from the player's cartridge by
 * blend/enemies/alttp_enemies.py into sprites 155 and 329-332. Every frame is a
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
        super->health = this->savedHealth;
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
        super->health = this->savedHealth;
        super->knockbackDuration = 0;
    } else if (IsHeavy(super->contactedEntity) && !this->coerced) {
        super->health = this->savedHealth;
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
    if (super->health < this->savedHealth) {
        if (gHk.mask < 3) {
            super->health = this->savedHealth;
            if (IsHeavy(by)) {
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
                super->health = this->savedHealth - 4;
            }
            EnqueueSFX(SFX_BOSS_HIT);
        }
    }
    EnemyFunctionHandlerAfterCollision(super, (void (*const*)())HelmasaurKing_Functions);
}

void HelmasaurKing(AlttpEnemy* this) {
    EnemyFunctionHandler(super, (EntityActionArray)HelmasaurKing_Functions);
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
        super->health = this->savedHealth;
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
