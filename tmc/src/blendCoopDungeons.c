/**
 * @file blendCoopDungeons.c
 *
 * @brief Co-op dungeons: with two players, every dungeon plays differently.
 *
 * A door on each dungeon's way to the boss stays shut until the two players open it
 * together, with Minish Cap's pressure plates:
 * - twin plates: two plates far apart, both down at once (one player can stand on one);
 * - a great plate: four plates in a square, down only when both players stand on it;
 * - hold and cross: one player holds a plate by the door, which stays open only while it
 *   is down; the other crosses and steps on a plate in the next room, which opens the
 *   door for good.
 * Each game checks the plates against its own Link and its partner (whose place the
 * co-op host sends), so both open the door at once. A door once opened stays open (a
 * bit in the save). Alone, nothing changes: no door, no plates.
 *
 * The A Link to the Past dungeons' locks are picked by blend/dungeons/coop.py
 * (custom/gen/coop.h); the Minish Cap dungeons' are below.
 */
#include "blend.h"
#include "area.h"
#include "asm.h"
#include "common.h"
#include "entity.h"
#include "flags.h"
#include "object.h"
#include "player.h"
#include "room.h"
#include "save.h"
#include "sound.h"

extern u32 ResolveCollisionLayer(Entity*);

enum { COOP_TWIN, COOP_GREAT, COOP_RELAY };

typedef struct {
    u8 room;
    u16 pos; /**< tile position in the room (x + y * 64) */
} CoopPlate;

typedef struct {
    u8 area;
    u8 room;
    u8 kind; /**< COOP_* */
    u8 dir;  /**< the door faces: 0 north, 1 east, 2 south, 3 west */
    u16 door; /**< tile position of the door (or the gate over stairs) */
    u8 count;
    CoopPlate plates[4]; /**< twin: 2; great: 4 (a square); relay: the hold plate, then the far one */
} CoopLock;

#include "../custom/gen/coop.h"

#define P(x, y) ((x) + ((y) << 6))

static const CoopLock sCoopLocks[] = {
    COOP_GEN_LOCKS
    // Deepwood Shrine: twin plates in the entrance hall open the way north; past the
    // key door, the lever room's way north to the barrel hall is held and crossed
    { AREA_DEEPWOOD_SHRINE, 11, COOP_TWIN, 0, P(10, 2), 2, { { 11, P(4, 11) }, { 11, P(16, 11) } } },
    { AREA_DEEPWOOD_SHRINE, 10, COOP_RELAY, 0, P(10, 2), 2, { { 10, P(7, 4) }, { 6, P(17, 20) } } },
    // Cave of Flames: a great plate between the entrance hall's boulders
    { AREA_CAVE_OF_FLAMES, 3, COOP_GREAT, 0, P(8, 2), 4,
      { { 3, P(7, 5) }, { 3, P(8, 5) }, { 3, P(7, 6) }, { 3, P(8, 6) } } },
    // Fortress of Winds: a gate over the stairs up from the middle hall, and twin plates
    // at the ends of its floor's cross
    { AREA_FORTRESS_OF_WINDS, 33, COOP_TWIN, 0, P(7, 2), 2, { { 33, P(7, 4) }, { 33, P(7, 8) } } },
    // Temple of Droplets: twin plates on the frozen hall's ring
    { AREA_TEMPLE_OF_DROPLETS, 3, COOP_TWIN, 0, P(16, 2), 2, { { 3, P(8, 6) }, { 3, P(24, 6) } } },
    // Dark Hyrule Castle: twin plates in the entrance hall open the west wing's door north
    { AREA_DARK_HYRULE_CASTLE, 0, COOP_TWIN, 0, P(16, 13), 2, { { 0, P(20, 24) }, { 0, P(30, 24) } } },
    // Royal Crypt: a great plate in the entrance
    { AREA_ROYAL_CRYPT, 8, COOP_GREAT, 0, P(8, 1), 4,
      { { 8, P(4, 4) }, { 8, P(5, 4) }, { 8, P(4, 5) }, { 8, P(5, 5) } } },
};

#define COOP_LOCKS ARRAY_COUNT(sCoopLocks)
/** the room flags the doors watch (high, past any room's own) */
#define COOP_ROOM_FLAG(slot) (0x8000 | (0x190 + (slot)))
#define MAX_DOORS 2
#define MAX_PLATES 8

typedef struct {
    u16 roomKey;
    u8 settle;
    u8 solvedNow;
    Entity* door[MAX_DOORS];
    Entity* plate[MAX_PLATES];
    u8 plateDown[MAX_PLATES]; /**< each plate's look (0xff: just made) */
} CoopDungeonState;

extern u8 gBlendCoopDungeons[];
#define gCoopDun (*(CoopDungeonState*)gBlendCoopDungeons)

static bool32 TwoPlayers(void) {
    return gBlendLink.hostConnected != 0;
}

static bool32 Opened(u32 i) {
    return (gSave.blendCoopLocks[i >> 3] >> (i & 7)) & 1;
}

static void Open(u32 i) {
    if (!Opened(i)) {
        gSave.blendCoopLocks[i >> 3] |= 1 << (i & 7);
        SoundReq(SFX_SECRET);
    }
}

/** Where a lock's plate n stands, in its room's pixels: a cell's middle, or for a great
 * plate the middle of its square of four cells. */
static void Spot(const CoopLock* l, u32 n, s32* x, s32* y) {
    u32 pos = l->plates[n].pos;
    *x = (pos & 0x3f) * 16 + (l->kind == COOP_GREAT ? 16 : 8);
    *y = (pos >> 6) * 16 + (l->kind == COOP_GREAT ? 16 : 8);
}

/** Feet within the plate (Minish Cap's plate for two is 32 pixels across). */
static bool32 Near(s32 x, s32 y, s32 sx, s32 sy, s32 r) {
    x -= sx;
    y -= sy;
    return x > -r && x < r && y > -r && y < r;
}

static bool32 LinkOn(const CoopLock* l, u32 n, s32 r) {
    Entity* p = &gPlayerEntity.base;
    s32 sx, sy;
    if (l->plates[n].room != gRoomControls.room || p->z.HALF.HI != 0) {
        return FALSE;
    }
    Spot(l, n, &sx, &sy);
    return Near(p->x.HALF.HI - gRoomControls.origin_x, p->y.HALF.HI - gRoomControls.origin_y, sx, sy, r);
}

/** The partner on a plate of a room in this area (each room has its own origin). */
static bool32 PartnerOn(const CoopLock* l, u32 n, s32 r) {
    BlendPlayerSync* p = &gBlendLink.partner;
    RoomHeader* h;
    s32 sx, sy;
    u32 room = l->plates[n].room;
    if (!TwoPlayers() || !p->active || p->area != gRoomControls.area || p->room != room || p->z != 0) {
        return FALSE;
    }
    h = gAreaRoomHeaders[gRoomControls.area] + room;
    Spot(l, n, &sx, &sy);
    return Near(p->x - h->map_x, p->y - h->map_y, sx, sy, r);
}

#define PLATE_REACH 12
#define GREAT_REACH 15

static bool32 Down(const CoopLock* l, u32 n) {
    return LinkOn(l, n, PLATE_REACH) || PartnerOn(l, n, PLATE_REACH);
}

/** How many plates a lock shows: a great plate is one. */
static u32 Plates(const CoopLock* l) {
    return l->kind == COOP_GREAT ? 1 : l->count;
}

/** Whether a lock's plates open its door now for good; *held: open only while held. */
static bool32 Check(const CoopLock* l, bool32* held) {
    *held = FALSE;
    switch (l->kind) {
        case COOP_TWIN:
            return Down(l, 0) && Down(l, 1);
        case COOP_GREAT:
            return LinkOn(l, 0, GREAT_REACH) && PartnerOn(l, 0, GREAT_REACH);
        case COOP_RELAY:
            *held = Down(l, 0);
            return Down(l, 1);
    }
    return FALSE;
}

/** The plate's look: 0 up, 1 one of two down, 4 all the way down. */
static u32 Look(const CoopLock* l, u32 n, u32 opened, bool32 held) {
    if (opened) {
        return 4;
    }
    switch (l->kind) {
        case COOP_TWIN:
            return Down(l, n) ? 1 : 0;
        case COOP_GREAT:
            return LinkOn(l, 0, GREAT_REACH) || PartnerOn(l, 0, GREAT_REACH) ? 1 : 0;
        default:
            return (n == 0 ? held : Down(l, n)) ? 4 : 0;
    }
}

static bool32 Alive(Entity* e, u32 id, u32 flag) {
    return e != NULL && e->next != NULL && e->kind == OBJECT && e->id == id &&
           *(u16*)((u8*)e + 0x86) == flag;
}

static void Forget(void) {
    MemClear(gCoopDun.door, sizeof(gCoopDun.door));
    MemClear(gCoopDun.plate, sizeof(gCoopDun.plate));
    MemClear(gCoopDun.plateDown, sizeof(gCoopDun.plateDown));
}

static Entity* Spawn(u32 id, u32 type, s32 x, s32 y, u32 flag) {
    Entity* e = CreateObject(id, type, 0);
    if (e == NULL) {
        return NULL;
    }
    e->x.HALF.HI = gRoomControls.origin_x + x;
    e->y.HALF.HI = gRoomControls.origin_y + y;
    // the layer the room's own objects there would take (Minish Cap draws some rooms'
    // floors on the upper layer)
    ResolveCollisionLayer(e);
    *(u16*)((u8*)e + 0x84) = 0xffff; // (a door: no flag deletes it)
    *(u16*)((u8*)e + 0x86) = flag;
    return e;
}

void Blend_CoopDungeonsUpdate(void) {
    u32 key = (gRoomControls.area << 8) | gRoomControls.room;
    u32 i, n, slot, doors = 0, plates = 0;
    bool32 ready;

    if (key != gCoopDun.roomKey) {
        // (the room's load clears what was spawned in the last room)
        gCoopDun.roomKey = key;
        gCoopDun.settle = 0;
        Forget();
    }
    if (gCoopDun.settle < 8 && gPlayerState.controlMode == CONTROL_ENABLED) {
        gCoopDun.settle++;
    }
    ready = gCoopDun.settle >= 8;

    for (i = 0; i < COOP_LOCKS; i++) {
        const CoopLock* l = &sCoopLocks[i];
        bool32 held, here = FALSE;
        u32 flag;
        if (l->area != gRoomControls.area) {
            continue;
        }
        for (n = 0; n < Plates(l); n++) {
            here |= l->plates[n].room == gRoomControls.room;
        }
        if (l->room != gRoomControls.room && !here) {
            continue;
        }
        if (TwoPlayers() && !Opened(i) && Check(l, &held)) {
            Open(i);
        }
        Check(l, &held);

        // the door: shut in co-op until solved (a hold plate opens it while held)
        if (l->room == gRoomControls.room && doors < MAX_DOORS) {
            slot = doors++;
            flag = COOP_ROOM_FLAG(slot);
            if (Opened(i) || !TwoPlayers() || held) {
                SetFlag(flag);
            } else {
                ClearFlag(flag);
            }
            if (TwoPlayers() && !Opened(i) && ready && !Alive(gCoopDun.door[slot], LOCKED_DOOR, flag)) {
                // it slams shut once Link is in the room (type 0x10), and a hold
                // plate's door shuts again when let go (0x80)
                gCoopDun.door[slot] = Spawn(LOCKED_DOOR, l->dir | 0x10 | (l->kind == COOP_RELAY ? 0x80 : 0),
                                            (l->door & 0x3f) * 16 + 8, (l->door >> 6) * 16 + 8, flag);
            }
        }

        // the plates in this room (only in co-op)
        for (n = 0; n < Plates(l); n++) {
            Entity* e;
            u32 look;
            s32 sx, sy;
            if (l->plates[n].room != gRoomControls.room || plates >= MAX_PLATES) {
                continue;
            }
            slot = plates++;
            flag = 0x8000 | (0x1a0 - 1 - slot); // (a mark, never set)
            e = gCoopDun.plate[slot];
            if (!TwoPlayers()) {
                if (Alive(e, PRESSURE_PLATE, flag)) {
                    DeleteEntity(e);
                }
                gCoopDun.plate[slot] = NULL;
                continue;
            }
            if (!Alive(e, PRESSURE_PLATE, flag)) {
                if (!ready) {
                    continue;
                }
                // Minish Cap's plate for two (type 0), held still past its init: this
                // file presses it
                Spot(l, n, &sx, &sy);
                e = Spawn(PRESSURE_PLATE, 0, sx, sy, flag);
                gCoopDun.plate[slot] = e;
                gCoopDun.plateDown[slot] = 0xff;
                if (e == NULL) {
                    continue;
                }
            }
            if (e->action == 1) {
                e->action = 2; // (canToggle is 0: it does nothing more by itself)
            }
            if (e->action != 2) {
                continue;
            }
            look = Look(l, n, Opened(i), held);
            if (look != gCoopDun.plateDown[slot]) {
                if (look > gCoopDun.plateDown[slot] && gCoopDun.plateDown[slot] != 0xff) {
                    SoundReq(look == 4 ? SFX_PRESSURE_PLATE : SFX_BUTTON_PRESS);
                }
                gCoopDun.plateDown[slot] = look;
                InitializeAnimation(e, look);
            }
        }
    }
}
