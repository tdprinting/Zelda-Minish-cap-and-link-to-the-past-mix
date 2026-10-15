#ifndef BLEND_COOP_H
#define BLEND_COOP_H

#include "global.h"

/**
 * Co-op for the Minish Cap x A Link to the Past blend: what the PC host
 * (blend/host/coop.c) needs on top of gBlendLink. Each player runs their own
 * copy of the game, with their own hearts, items and Rupees.
 *
 * - A player whose hearts run out goes down instead of getting a game over,
 *   and waits for the partner to come and revive them with a Revive Fairy.
 *   The game ends only when both are down (or the one down gives up).
 * - Revive Fairies are sold in Stockwell's shop.
 * - The host's quick inventory reads the menu from here and asks the game to
 *   equip items, while the game keeps running.
 * - The host shares progress between the two games (see coop.c).
 *
 * Lives at a fixed EWRAM address (0x0203F800, inside gBlendLink's page) so the
 * host can find it. The host reads and writes it by these offsets: keep them.
 */
typedef struct {
    /*0x00*/ u32 magic;            /**< BLEND_COOP_MAGIC once the game has set the block up */
    /*0x04*/ u8 version;           /**< BLEND_COOP_VERSION */
    /*0x05*/ u8 heartbeat;         /**< counts gameplay frames */
    /*0x06*/ u8 downed;            /**< this player is down, waiting to be revived */
    /*0x07*/ u8 reviveFairies;     /**< Revive Fairies this player carries */
    /*0x08*/ u8 reviveSeq;         /**< counts the revives this player has given the partner */
    /*0x09*/ u8 partnerReviveSeq;  /**< host: the partner's reviveSeq */
    /*0x0a*/ u8 partnerDowned;     /**< host: the partner is down */
    /*0x0b*/ u8 partnerInGame;     /**< host: the partner is playing (in the game, its menus included) */
    /*0x0c*/ u8 seenReviveSeq;     /**< the partner's last revive this game acted on */
    /*0x0d*/ u8 reviveNear;        /**< the partner is down within reach: R revives them */
    /*0x0e*/ u8 giveUpTimer;       /**< frames the downed player has held A + B to give up */
    /*0x0f*/ u8 gaveUp;            /**< the downed player gave up: the game goes on to the game over */
    /*0x10*/ u32 saveAddr;         /**< &gSave */
    /*0x14*/ u32 mainAddr;         /**< &gMain */
    /*0x18*/ u32 itemFramesAddr;   /**< gSpritePtrs[ITEM_ICON_SPRITE]: the item pictures */
    /*0x1c*/ u32 frameObjListsAddr; /**< gFrameObjLists */
    /*0x20*/ u8 quickMenu;         /**< host: this player's quick inventory is open */
    /*0x21*/ u8 quickEquip;        /**< host: menu entry + 1 to equip... */
    /*0x22*/ u8 quickEquipSlot;    /**< ...on this button (0 = A, 1 = B); the game clears quickEquip */
    /*0x23*/ u8 shareGive;         /**< host: an item the partner found, to give quietly; the game clears it */
    /*0x24*/ u8 regroup;           /**< host: go to where the partner is; the game clears it */
    /*0x25*/ u8 menuCount;         /**< entries in the quick inventory */
    /*0x26*/ u8 equippedA;         /**< the entries on A and B (+ 1, 0 for none), for the host's menu */
    /*0x27*/ u8 equippedB;
    /*0x28*/ u8 menuItem[24];      /**< each entry's item (an A Link to the Past item where one shares the slot) */
    /*0x40*/ u8 menuIcon[24];      /**< the item whose picture shows it */
    /*0x58*/ u8 menuFrame[24];     /**< that picture's frame in the item sprite */
    /*0x70*/ u16 roomX;            /**< Link's position in the room, for the partner's regroup */
    /*0x72*/ u16 roomY;
    /*0x74*/ u8 layer;
    /*0x75*/ u8 itemPalette;       /**< OBJ palette row the item pictures use */
    /*0x76*/ u8 partnerRoomOk;     /**< host: the partner's position (roomX/Y, area, room) is current */
    /*0x77*/ u8 regroupFailed;     /**< the last regroup could not happen (Link was busy); cleared by the host */
    /*0x78*/ u16 partnerRoomX;     /**< host: the partner's roomX/roomY/layer */
    /*0x7a*/ u16 partnerRoomY;
    /*0x7c*/ u8 partnerLayer;
    /*0x7d*/ u8 bombs;
    /*0x7e*/ u8 arrows;
    /*0x7f*/ u8 reviveCooldown;   /**< frames before Link can revive again (the partner's game is catching up) */
    /*0x80*/ u8 menuEquip[24];     /**< game only: the item each entry equips */
    /*0x98*/ u8 menuVariant[24];   /**< game only: the shared-slot item it switches on, 0xff to switch it off, or 0 */
    /*0xb0*/ u8 revivePalette;     /**< game only: the OBJ palette slot holding the Revive Fairy's colors, + 1 */
    /*0xb1*/ u8 reviveFxTimer;     /**< game only: sparkles around a player just revived */
    /*0xb2*/ u8 messageOpen;      /**< a text box is up (the host moves its own text out of the way) */
    /* Carrying the partner: this game's Link lifts the partner's (CARRY_*)... */
    /*0xb3*/ u8 carryState;
    /*0xb4*/ u16 carryX;           /**< ...where the partner is held or flies (world coordinates)... */
    /*0xb6*/ u16 carryY;
    /*0xb8*/ s8 carryZ;            /**< ...how high */
    /*0xb9*/ u8 carrySeq;          /**< counts landings */
    /*0xba*/ u8 partnerCarryState; /**< host: the partner's carryState, carryX/Y/Z and carrySeq */
    /*0xbb*/ u8 partnerCarrySeq;
    /*0xbc*/ u16 partnerCarryX;
    /*0xbe*/ u16 partnerCarryY;
    /*0xc0*/ s8 partnerCarryZ;
    /*0xc1*/ u8 carried;           /**< game only: this Link is held or thrown by the partner */
    /*0xc2*/ u8 liftable;          /**< this Link can be picked up (free, on foot, not down) */
    /*0xc3*/ u8 partnerLiftable;   /**< host: the partner's liftable */
    /* Giving the partner a heart (L + R next to them) */
    /*0xc4*/ u8 heartGiveSeq;      /**< counts hearts given */
    /*0xc5*/ u8 partnerHeartGiveSeq; /**< host: the partner's heartGiveSeq */
    /*0xc6*/ u8 seenHeartGiveSeq;
    /*0xc7*/ u8 partnerHealth;     /**< host: the partner's hearts, as gSave.stats */
    /*0xc8*/ u8 partnerMaxHealth;
    /* Co-op puzzles (blendCoopPuzzles.c) */
    /*0xc9*/ u8 piecesLit;         /**< the pieces of this room's puzzle this game has lit (bits) */
    /*0xca*/ u8 partnerPiecesLit;  /**< host: the partner's piecesLit, when in the same room */
    /*0xcb*/ u8 seenCarrySeq;      /**< game only: the partner's last landing this game acted on */
    /*0xcc*/ u16 puzzleRoomKey;    /**< game only: area << 8 | room the puzzle pieces were made for, + 1 */
    /*0xce*/ u8 puzzleSolved;      /**< game only: this room's puzzle was solved this visit */
    /*0xcf*/ u8 pieceTimer[8];     /**< game only: frames each lit Twin Stone stays lit */
    /*0xd7*/ u8 pad_d7;
    /*0xd8*/ struct Entity_* reward; /**< game only: the reward a solved puzzle left */
    /*0xdc*/ u8 carryGrace;        /**< game only: frames to keep following after the partner's carry ends */
    /*0xdd*/ u8 pad_dd[3];
} BlendCoop;

enum {
    CARRY_NONE,
    CARRY_HOLD,   /**< the partner is held up over Link's head */
    CARRY_FLY,    /**< thrown, in the air */
    CARRY_LANDED, /**< came down at carryX/carryY */
};

#define BLEND_COOP_MAGIC 0x504F4F43 /* "COOP" */
#define BLEND_COOP_VERSION 1

extern BlendCoop gBlendCoop;

/** The Revive Fairy's sprite (blend/coop/revive_fairy.py; keep in step with sprite_ptrs.s). */
#define REVIVE_FAIRY_SPRITE 338
/** Revive Fairies carried, in an unused byte of the save file. */
#define BLEND_REVIVE_FAIRIES (gSave.fillerA[21])
#define REVIVE_FAIRIES_MAX 9
/** The Twin Stone and the Toss Bell (blend/coop/twin_relics.py; keep in step with sprite_ptrs.s). */
#define TWIN_RELICS_SPRITE 339
/** Co-op puzzles solved, and their rewards taken (each player's own; bits by puzzle). */
#define BLEND_COOP_PUZZLES_SOLVED (gSave.filler2E[16])
#define BLEND_COOP_PUZZLES_TAKEN (gSave.filler2E[17])

void Blend_CoopUpdate(void);
/** While Link lies down after his hearts ran out: -1 to keep waiting, 0 for the game over, or the hearts to revive with. */
s32 Blend_CoopWhileDown(void);
/** After an item picture object loads its sprite: the Revive Fairy swaps in its own. */
void Blend_CoopItemPicture(struct Entity_* this);
/** The animation an item picture shows (the Revive Fairy's own, or the item's). */
u32 Blend_CoopItemAnim(struct Entity_* this);
void Blend_GiveReviveFairy(void);
/** The partner Link object's type 4: the Revive Fairy floating over a downed partner. */
void Blend_CoopReviveMarker(struct Entity_* this);
/** The partner is down (the partner Link is drawn as a flickering ghost). */
bool32 Blend_CoopPartnerDowned(void);
/** This OBJ palette row is the Revive Fairy's: the Dark World leaves it alone. */
bool32 Blend_CoopKeepsPalette(u32 palIdx);
/** The partner Link object (type 0) in this game: can be lifted and thrown. Returns TRUE while carried. */
bool32 Blend_CoopPartnerCarry(struct Entity_* this);
/** The partner Link object's types 5 and 6: a Twin Stone and a Toss Bell. */
void Blend_CoopPuzzlePiece(struct Entity_* this);
void Blend_CoopPuzzlesUpdate(void);

#endif // BLEND_COOP_H
