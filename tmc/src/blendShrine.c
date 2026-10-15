/**
 * @file blendShrine.c
 *
 * @brief The Temple of Bonds: the blend's co-op temple, open only to two players.
 *
 * A temple made of a chamber from every temple (blend/dungeons/shrine.py builds them),
 * joined by steps, with puzzles two players solve together:
 * - plates: a twin plate group is solved once all its plates are down at once (one
 *   player can stand on only one); a hold group keeps its gates open only while one of
 *   its plates is down, so one player holds the way open for the other.
 * - gates and bridges: open for good once their puzzle is solved, or while their hold
 *   group is down. A gate waits for Link to step out of it before it shuts.
 * - twin seals: crystals, often in two different chambers, that must be struck within
 *   a moment of each other.
 * - levers: crystals one strike solves for good, so a player who held a gate open
 *   can follow.
 * Each game keeps the puzzles it has solved as local flags of the temple and tells the
 * other game through BlendPlayerSync.shrine, which also carries the seal it just struck.
 *
 * A guard stands at the temple's cave in South Hyrule Field: alone, Link can't get by;
 * with a partner, the guard steps aside.
 *
 * Each chamber has a full set of metatiles to itself: Blend_RoomTiles hands the game the
 * current room's.
 */
#include "blend.h"
#include "area.h"
#include "common.h"
#include "entity.h"
#include "flags.h"
#include "main.h"
#include "map.h"
#include "message.h"
#include "npc.h"
#include "player.h"
#include "room.h"
#include "sound.h"
#include "transitions.h"
#include "tiles.h"
#include "asm.h"

#include "../custom/gen/shrine.h"

extern void* gAreaTiles[];
extern void* const gShrineRoomTiles[];

/** frames a struck seal stays lit, waiting for its twin */
#define SEAL_WINDOW 90
/** the guard's dialogs in gUnk_0810CF4C (guard.c): to one player, to two */
#define GUARD_DIALOG_SOLO 43
#define GUARD_DIALOG_COOP 44
/** where the guard stands in South Hyrule Field: before the cave, or beside it */
#define CAVE_X 0x58
#define CAVE_Y 0x118
#define GUARD_X 0x58
#define GUARD_Y 0x134
#define GUARD_ASIDE_X 0x30
#define GUARD_ASIDE_Y 0x12c

typedef struct {
    u8 swinging;     /**< Link was swinging the sword last frame */
    u8 struck;       /**< the seal Link struck last (+1), published while it is lit */
    u8 struckTimer;
    u8 guardAside;
    Entity* guard;
    u8 lit[16];        /**< frames each seal (in order) stays lit from Link's strike */
    u8 partnerLit[16]; /**< and from the partner's */
    u8 pending[16];    /**< gates (by device index) waiting for Link to step out before they shut */
    u8 down[8];        /**< hold groups with a plate down this frame */
} ShrineState;

extern u8 gBlendPegsState[];
// after the pegs' state (blend.c), in the same reserved block of EWRAM
#define gShrine (*(ShrineState*)(gBlendPegsState + 0x80))

void* Blend_RoomTiles(u32 area, u32 room) {
    if (area == SHRINE_AREA && room < SHRINE_ROOMS) {
        return gShrineRoomTiles[room];
    }
    return gAreaTiles[area];
}

static bool32 TwoPlayers(void) {
    return gBlendLink.hostConnected != 0;
}

bool32 Blend_ShrineClosed(const Transition* t) {
    return t->area == SHRINE_AREA && !TwoPlayers();
}

static bool32 Solved(u32 bit) {
    return bit != 0xff && CheckLocalFlagByBank(FLAG_BANK_21, SHRINE_BIT_FLAG + bit);
}

static void Solve(u32 bit) {
    if (bit != 0xff && !Solved(bit)) {
        SetLocalFlagByBank(FLAG_BANK_21, SHRINE_BIT_FLAG + bit);
        if (gRoomControls.area == SHRINE_AREA) {
            SoundReq(SFX_SECRET_BIG);
        }
    }
}

static u32 SolvedBits(void) {
    u32 bits = 0, i;
    for (i = 0; i < 12; i++) {
        if (Solved(i)) {
            bits |= 1 << i;
        }
    }
    return bits;
}

/** Whether a point (in room pixels) stands on a cell, as a tile position. */
static bool32 OnCell(s32 x, s32 y, u32 pos) {
    x -= (pos & 0x3f) * 16;
    y -= (pos >> 6) * 16;
    return x >= 0 && x < 16 && y >= 0 && y < 16;
}

static bool32 LinkOnPlate(u32 pos) {
    Entity* p = &gPlayerEntity.base;
    return p->z.HALF.HI == 0 &&
           OnCell(p->x.HALF.HI - gRoomControls.origin_x, p->y.HALF.HI - gRoomControls.origin_y - 2, pos);
}

static bool32 PartnerHere(void) {
    BlendPlayerSync* p = &gBlendLink.partner;
    return TwoPlayers() && p->active && p->area == gRoomControls.area && p->room == gRoomControls.room;
}

static bool32 PartnerOnPlate(u32 pos) {
    BlendPlayerSync* p = &gBlendLink.partner;
    return PartnerHere() && p->z == 0 &&
           OnCell(p->x - gRoomControls.origin_x, p->y - gRoomControls.origin_y - 2, pos);
}

/** Whether Link stands (partly) in a cell: a gate shutting there waits. */
static bool32 LinkInCell(u32 pos) {
    s32 x = gPlayerEntity.base.x.HALF.HI - gRoomControls.origin_x - (pos & 0x3f) * 16;
    s32 y = gPlayerEntity.base.y.HALF.HI - gRoomControls.origin_y - (pos >> 6) * 16;
    return x > -6 && x < 16 + 6 && y > -2 && y < 16 + 6;
}

static void Show(u32 pos, u32 meta) {
    if (GetLayerByIndex(LAYER_BOTTOM)->mapData[pos] != meta) {
        SetTileByIndex(meta, pos, LAYER_BOTTOM);
    }
}

/** The order of a seal among the temple's seals. */
static u32 SealIndex(const ShrineDevice* d) {
    const ShrineDevice* s;
    u32 n = 0;
    for (s = sShrineDevices; s != d; s++) {
        if (s->kind == SHRINE_SEAL) {
            n++;
        }
    }
    return n;
}

/** Twin seals: a pair is solved once each of its seals is lit, by either player. */
static void CheckPairs(void) {
    u32 pair;
    for (pair = 0; pair < ARRAY_COUNT(sShrinePairs); pair++) {
        const ShrineDevice* d;
        u32 n = 0, all = TRUE;
        if (Solved(sShrinePairs[pair])) {
            continue;
        }
        for (d = sShrineDevices; d->room != 0xff; d++) {
            if (d->kind == SHRINE_SEAL) {
                if (d->a == pair && n < 16 && !gShrine.lit[n] && !gShrine.partnerLit[n]) {
                    all = FALSE;
                }
                n++;
            }
        }
        if (all) {
            Solve(sShrinePairs[pair]);
        }
    }
}

/** A sword swing just began: strikes the seal or lever in front of Link. */
static void Strike(void) {
    const ShrineDevice* d;
    s32 x, y;
    u32 facing = (gPlayerEntity.base.animationState >> 1) & 3;
    static const s8 dx[] = { 0, 1, 0, -1 };
    static const s8 dy[] = { -1, 0, 1, 0 };
    x = gPlayerEntity.base.x.HALF.HI - gRoomControls.origin_x + dx[facing] * 12;
    y = gPlayerEntity.base.y.HALF.HI - gRoomControls.origin_y + dy[facing] * 12;
    for (d = sShrineDevices; d->room != 0xff; d++) {
        s32 ex, ey;
        if (d->room != gRoomControls.room || (d->kind != SHRINE_SEAL && d->kind != SHRINE_LEVER)) {
            continue;
        }
        ex = x - ((d->pos & 0x3f) * 16 + 8);
        ey = y - ((d->pos >> 6) * 16 + 8);
        if (ex <= -14 || ex >= 14 || ey <= -14 || ey >= 14) {
            continue;
        }
        SoundReq(SFX_METAL_CLINK);
        if (d->kind == SHRINE_LEVER) {
            Solve(d->a);
        } else {
            u32 n = SealIndex(d);
            if (n < 16) {
                gShrine.lit[n] = SEAL_WINDOW;
                gShrine.struck = n + 1;
                gShrine.struckTimer = SEAL_WINDOW;
            }
        }
        break;
    }
}

static void UpdateDevices(void) {
    const ShrineDevice* d;
    u32 i, n, swing;
    u8 held[8];
    u8 pressed[8];
    u8 plates[8];

    // the plates: which groups have a plate down, and which have all theirs down
    MemClear(held, sizeof(held));
    MemClear(pressed, sizeof(pressed));
    MemClear(plates, sizeof(plates));
    for (d = sShrineDevices; d->room != 0xff; d++) {
        if (d->room == gRoomControls.room && d->kind == SHRINE_PLATE && d->a < 8) {
            bool32 down = LinkOnPlate(d->pos) || PartnerOnPlate(d->pos);
            plates[d->a]++;
            if (down) {
                pressed[d->a]++;
                held[d->a] = 1;
            }
            if (down && !(gShrine.down[d->a])) {
                SoundReq(SFX_PRESSURE_PLATE);
            }
        }
    }
    for (i = 0; i < 8 && i < ARRAY_COUNT(sShrineGroups); i++) {
        if (plates[i] != 0 && pressed[i] == plates[i]) {
            Solve(sShrineGroups[i]);
        }
        gShrine.down[i] = held[i];
    }

    swing = gPlayerState.framestate == PL_STATE_SWORD;
    if (swing && !gShrine.swinging) {
        Strike();
    }
    gShrine.swinging = swing;
    CheckPairs();

    // show each device as it stands
    n = 0;
    for (i = 0, d = sShrineDevices; d->room != 0xff; d++, i++) {
        u32 on = FALSE;
        if (d->kind == SHRINE_SEAL) {
            on = Solved(sShrinePairs[d->a]) || (n < 16 && (gShrine.lit[n] || gShrine.partnerLit[n]));
            n++;
        }
        if (d->room != gRoomControls.room) {
            continue;
        }
        switch (d->kind) {
            case SHRINE_PLATE:
                on = (d->a < 8 && gShrine.down[d->a]) || Solved(sShrineGroups[d->a]);
                break;
            case SHRINE_GATE:
                on = Solved(d->a) || (d->b < 8 && gShrine.down[d->b]);
                break;
            case SHRINE_LEVER:
                on = Solved(d->a);
                break;
        }
        if (d->kind == SHRINE_GATE && !on && i < 128) {
            // a gate shutting waits for Link to step out of it
            if (GetLayerByIndex(LAYER_BOTTOM)->mapData[d->pos] != d->meta0 && LinkInCell(d->pos)) {
                gShrine.pending[i >> 3] |= 1 << (i & 7);
                continue;
            }
            if (gShrine.pending[i >> 3] & (1 << (i & 7))) {
                gShrine.pending[i >> 3] &= ~(1 << (i & 7));
                SoundReq(SFX_METAL_CLINK);
            }
        }
        Show(d->pos, on ? d->meta1 : d->meta0);
    }
}

/** The seals each game has lit, and the puzzles each has solved, go to the other. */
static void Sync(void) {
    u32 i, bits, seal;
    for (i = 0; i < 16; i++) {
        if (gShrine.lit[i]) {
            gShrine.lit[i]--;
        }
        if (gShrine.partnerLit[i]) {
            gShrine.partnerLit[i]--;
        }
    }
    if (gShrine.struckTimer && --gShrine.struckTimer == 0) {
        gShrine.struck = 0;
    }
    if (TwoPlayers() && gBlendLink.partner.active) {
        bits = gBlendLink.partner.shrine & 0xfff;
        for (i = 0; i < 12; i++) {
            if (bits & (1 << i)) {
                Solve(i);
            }
        }
        seal = gBlendLink.partner.shrine >> 12;
        if (seal != 0 && seal <= 16) {
            gShrine.partnerLit[seal - 1] = SEAL_WINDOW;
        }
    }
    gBlendLink.self.shrine = SolvedBits() | (gShrine.struck << 12);
}

static bool32 IsGuard(Entity* e) {
    return e != NULL && e->next != NULL && e->kind == NPC && e->id == GUARD;
}

/**
 * The temple's cave: the little cave west of Link's house, which a Kinstone fusion opens in
 * Minish Cap, stands open (its 5x5 cells of cave mouth, as the fusion makes them).
 */
static void OpenCave(void) {
    u32 pos = TILE_POS(CAVE_X / 16, CAVE_Y / 16), i, j, type = TILE_TYPE_512;
    if (GetTileTypeAtTilePos(pos, LAYER_BOTTOM) == TILE_TYPE_512 + 12) {
        return;
    }
    pos -= TILE_POS(2, 2);
    for (i = 0; i < 5; i++, pos += 0x40) {
        for (j = 0; j < 5; j++) {
            SetTileType(type++, pos + j, LAYER_BOTTOM);
        }
    }
}

/** The guard at the temple's cave: before it with one player, beside it with two. */
static void UpdateGuard(void) {
    Entity* g;
    u32 aside = TwoPlayers();
    if (gRoomControls.area != AREA_HYRULE_FIELD || gRoomControls.room != ROOM_HYRULE_FIELD_SOUTH_HYRULE_FIELD ||
        gPlayerState.controlMode != CONTROL_ENABLED) {
        if (gRoomControls.area != AREA_HYRULE_FIELD || gRoomControls.room != ROOM_HYRULE_FIELD_SOUTH_HYRULE_FIELD) {
            gShrine.guard = NULL;
        }
        return;
    }
    OpenCave();
    g = gShrine.guard;
    if (!IsGuard(g)) {
        g = CreateNPC(GUARD, 4, aside ? GUARD_DIALOG_COOP : GUARD_DIALOG_SOLO);
        if (g == NULL) {
            return;
        }
        g->x.HALF.HI = gRoomControls.origin_x + (aside ? GUARD_ASIDE_X : GUARD_X);
        g->y.HALF.HI = gRoomControls.origin_y + (aside ? GUARD_ASIDE_Y : GUARD_Y);
        g->collisionLayer = 1;
        gShrine.guard = g;
        gShrine.guardAside = aside;
        return;
    }
    // he stands his ground, facing the field (no patrol)
    if (g->action != 0) {
        g->child = NULL;
    }
    g->type2 = aside ? GUARD_DIALOG_COOP : GUARD_DIALOG_SOLO;
    if (aside != gShrine.guardAside && g->action == 1) {
        gShrine.guardAside = aside;
        g->x.HALF.HI = gRoomControls.origin_x + (aside ? GUARD_ASIDE_X : GUARD_X);
        g->y.HALF.HI = gRoomControls.origin_y + (aside ? GUARD_ASIDE_Y : GUARD_Y);
    }
    if (g->action == 1 && g->interactType == INTERACTION_NONE && g->knockbackSpeed == 0) {
        g->knockbackDirection = aside ? 1 : 2;
    }
}

void Blend_ShrineUpdate(void) {
    Sync();
    UpdateGuard();
    if (gRoomControls.area == SHRINE_AREA) {
        UpdateDevices();
    } else {
        MemClear(gShrine.pending, sizeof(gShrine.pending));
        MemClear(gShrine.down, sizeof(gShrine.down));
    }
}
