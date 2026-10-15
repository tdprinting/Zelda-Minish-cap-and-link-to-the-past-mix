// Split-screen co-op host for the Minish Cap x A Link to the Past blend.
//
// Runs two copies of the blended game side by side in one window, one per
// player. Each player has their own game: their own hearts, items, Rupees,
// equipment and menus. The host ties them together:
// - Every frame it copies each player's position and animation into the
//   other game's shared state block (gBlendLink at 0x0203F000), where the game
//   draws them as a second Link in red mail.
// - Going down: a player whose hearts run out waits for the partner to revive
//   them with a Revive Fairy (sold in Stockwell's shop). The game is over only
//   when both are down. The host passes each game the other's state through
//   gBlendCoop (0x0203F800, see tmc/include/blendCoop.h).
// - Shared progress: chests, doors, bosses, story and keys, and the items one
//   player finds, reach the other game too (see SyncProgress).
// - Quick inventory: each player can open their own item menu over their half
//   of the screen. Their game keeps running at half speed while it is open,
//   and the partner's game does not slow at all.
//
// Usage: zelda-blend [game.gba] [--single] [--vertical] [--no-update]
//        zelda-blend game.gba --test SCRIPT OUT.ppm   (headless test, see runScript)
//        zelda-blend --update-only [--update-from FOLDER|ZIP|URL|github:owner/repo]
//                                   (check for and install a newer build, then exit)
//        zelda-blend --version
//
// On Windows it first looks for a newer build and offers to install it (updater.c).
//
// Controls (keyboard; gamepads 1 and 2 also work):
//   Player 1: WASD move, J = B, K = A, U = L, I = R, Enter = Start, Right Shift = Select,
//             Q = quick inventory
//   Player 2: arrows move, Numpad 1 = B, Numpad 2 = A, Numpad 4 = L, Numpad 5 = R,
//             Numpad Enter = Start, Numpad + = Select, Numpad 0 = quick inventory
//   Gamepad: Y (or the top face button) opens the quick inventory.
//   F1 toggles side-by-side and stacked layout, F11 toggles fullscreen, Esc quits.
#include <mgba/core/core.h>
#include <mgba/core/blip_buf.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba-util/vfs.h>

#include <SDL.h>
#include <stdarg.h>
#include "updater.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "revive_art.h"

#define GBA_W 240
#define GBA_H 160
#define SCALE 3
#define AUDIO_RATE 44100
// The overlay (quick inventory, prompts) is drawn at twice the game's resolution.
#define OV 2
#define OV_W (GBA_W * OV)
#define OV_H (GBA_H * OV)

// Layout of gBlendLink (see tmc/include/blend.h)
#define LINK_BASE 0x0203F000u
#define LINK_HOST_CONNECTED (LINK_BASE + 0x04)
#define LINK_PLAYER_INDEX (LINK_BASE + 0x05)
#define LINK_SELF (LINK_BASE + 0x10)
#define LINK_PARTNER (LINK_BASE + 0x34)
#define LINK_SYNC_SIZE 0x24
#define SYNC_X 0x00
#define SYNC_Y 0x02
#define SYNC_AREA 0x04
#define SYNC_ROOM 0x05
#define SYNC_DARK_WORLD 0x07
#define SYNC_ACTIVE 0x0b

// Layout of gBlendCoop (see tmc/include/blendCoop.h)
#define COOP 0x0203F800u
#define COOP_MAGIC 0x504F4F43u
#define COOP_VERSION 1
enum {
	C_MAGIC = 0x00, C_VERSION = 0x04, C_HEARTBEAT = 0x05, C_DOWNED = 0x06, C_FAIRIES = 0x07,
	C_REVIVE_SEQ = 0x08, C_PARTNER_REVIVE_SEQ = 0x09, C_PARTNER_DOWNED = 0x0a, C_PARTNER_IN_GAME = 0x0b,
	C_REVIVE_NEAR = 0x0d, C_GIVE_UP_TIMER = 0x0e, C_SAVE = 0x10, C_MAIN = 0x14, C_ITEM_FRAMES = 0x18,
	C_FRAME_OBJ_LISTS = 0x1c, C_QUICK_MENU = 0x20, C_QUICK_EQUIP = 0x21, C_QUICK_EQUIP_SLOT = 0x22,
	C_SHARE_GIVE = 0x23, C_REGROUP = 0x24, C_MENU_COUNT = 0x25, C_EQUIPPED_A = 0x26, C_EQUIPPED_B = 0x27,
	C_MENU_ITEM = 0x28, C_MENU_ICON = 0x40, C_MENU_FRAME = 0x58, C_ROOM_X = 0x70, C_ROOM_Y = 0x72,
	C_LAYER = 0x74, C_ITEM_PALETTE = 0x75, C_PARTNER_ROOM_OK = 0x76, C_REGROUP_FAILED = 0x77,
	C_PARTNER_ROOM_X = 0x78, C_PARTNER_ROOM_Y = 0x7a, C_PARTNER_LAYER = 0x7c, C_BOMBS = 0x7d, C_ARROWS = 0x7e,
	C_MESSAGE_OPEN = 0xb2, C_CARRY_STATE = 0xb3, C_CARRY_X = 0xb4, C_CARRY_Y = 0xb6, C_CARRY_Z = 0xb8,
	C_CARRY_SEQ = 0xb9, C_PARTNER_CARRY_STATE = 0xba, C_PARTNER_CARRY_SEQ = 0xbb, C_PARTNER_CARRY_X = 0xbc,
	C_PARTNER_CARRY_Y = 0xbe, C_PARTNER_CARRY_Z = 0xc0, C_CARRIED = 0xc1, C_LIFTABLE = 0xc2, C_PARTNER_LIFTABLE = 0xc3,
	C_HEART_SEQ = 0xc4, C_PARTNER_HEART_SEQ = 0xc5, C_PARTNER_HEALTH = 0xc7, C_PARTNER_MAX_HEALTH = 0xc8,
	C_PIECES_LIT = 0xc9, C_PARTNER_PIECES_LIT = 0xca, C_MENU_PAGE = 0xdd,
};
#define MENU_MAX 24
#define REVIVE_FAIRIES_MAX 9
#define GIVE_UP_FRAMES 120
#define TASK_GAME 2
// the item sprite (its frame object lists sit under this index in gFrameObjLists)
#define ITEM_SPRITE 322

// Layout of the save file, gSave (see tmc/include/save.h)
enum {
	S_GLOBAL_PROGRESS = 0x08, S_STORY = 0x0c, S_BLEND_KEYS = 0x22, S_BLEND_ITEMS = 0x26, S_BLEND_WARPS = 0x2a,
	S_WINDCRESTS = 0x40, S_AREA_VISITS = 0x60, S_STATS = 0xa8, S_INVENTORY = 0xf2, S_FLAGS = 0x25c,
	S_DUNGEON_KEYS = 0x45c, S_DUNGEON_ITEMS = 0x46c, S_DUNGEON_WARPS = 0x47c,
	ST_WALLET = 0x00, ST_HEART_PIECES = 0x01, ST_HEALTH = 0x02, ST_MAX_HEALTH = 0x03, ST_BOMB_BAG = 0x06,
	ST_QUIVER = 0x07, ST_RUPEES = 0x18,
};
#define INVENTORY_BYTES 34

enum {
	GBA_A = 1 << 0, GBA_B = 1 << 1, GBA_SELECT = 1 << 2, GBA_START = 1 << 3,
	GBA_RIGHT = 1 << 4, GBA_LEFT = 1 << 5, GBA_UP = 1 << 6, GBA_DOWN = 1 << 7,
	GBA_R = 1 << 8, GBA_L = 1 << 9,
};

// Bit ranges of the save file whose bits only ever get set as the story goes on:
// what one player does there (a chest opened, a door unlocked, a boss beaten)
// happens in the partner's game too.
static const struct { uint32_t offset, size; } sSharedBits[] = {
	{ S_FLAGS, 0x200 }, { S_AREA_VISITS, 32 }, { S_WINDCRESTS, 4 }, { S_DUNGEON_ITEMS, 16 },
	{ S_DUNGEON_WARPS, 16 }, { S_BLEND_ITEMS, 4 }, { S_BLEND_WARPS, 4 }, { S_STORY, 1 },
};
#define SHARED_BIT_BYTES (0x200 + 32 + 4 + 16 + 16 + 4 + 4 + 1)
// Small keys: both players draw from one key ring per dungeon.
static const struct { uint32_t offset, size; } sSharedKeys[] = { { S_DUNGEON_KEYS, 16 }, { S_BLEND_KEYS, 4 } };
#define SHARED_KEY_BYTES 20

struct Toast {
	char text[96];
	int frames;
};

struct Player {
	struct mCore* core;
	color_t* video;
	SDL_GameController* pad;
	uint32_t* overlay; // OV_W * OV_H, 0xAABBGGRR like the game's frames
	int index;
	// quick inventory
	int menuOpen, cursor;
	uint16_t prevKeys;
	uint16_t heldAtClose; // keys still held from the menu, kept from the game until released
	int repeat;
	// progress sharing
	int synced;
	uint8_t snapBits[SHARED_BIT_BYTES];
	uint8_t snapKeys[SHARED_KEY_BYTES];
	uint8_t snapInv[INVENTORY_BYTES];
	int snapHearts;
	uint8_t giveQueue[32];
	int giveCount;
	int giveWaiting; // an item sits in the game's mailbox
	struct Toast toast;
	int wasDowned;
	int hintFrames; // how long the "R: lift" hint has shown (it stops after a while)
};

static struct Player p[2];
static int players = 2;
static unsigned tick;

static void quietLog(struct mLogger* logger, int category, enum mLogLevel level, const char* format, va_list args) {
	(void) logger; (void) category;
	if (level & (mLOG_FATAL | mLOG_ERROR)) {
		vfprintf(stderr, format, args);
		fputc('\n', stderr);
	}
}
static struct mLogger logger = { .log = quietLog };

/* ---- Memory -------------------------------------------------------------- */

static uint8_t r8(struct Player* pl, uint32_t a) { return pl->core->busRead8(pl->core, a); }
static uint16_t r16(struct Player* pl, uint32_t a) { return pl->core->busRead16(pl->core, a); }
static uint32_t r32(struct Player* pl, uint32_t a) { return pl->core->busRead32(pl->core, a); }
static void w8(struct Player* pl, uint32_t a, uint8_t v) { pl->core->busWrite8(pl->core, a, v); }
static void w16(struct Player* pl, uint32_t a, uint16_t v) { pl->core->busWrite16(pl->core, a, v); }

static int coopReady(struct Player* pl) {
	return r32(pl, COOP + C_MAGIC) == COOP_MAGIC && r8(pl, COOP + C_VERSION) == COOP_VERSION;
}

static uint32_t saveAddr(struct Player* pl) { return r32(pl, COOP + C_SAVE); }

/** The player's game is in play: a file is loaded and the game (or its menus) is running. */
static int inGame(struct Player* pl) {
	uint32_t mainAddr;
	if (!coopReady(pl))
		return 0;
	mainAddr = r32(pl, COOP + C_MAIN);
	return (mainAddr >> 24) == 3 && r8(pl, mainAddr + 2) == TASK_GAME;
}



static void toast(struct Player* pl, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(pl->toast.text, sizeof(pl->toast.text), fmt, ap);
	va_end(ap);
	pl->toast.frames = 240;
}

/* ---- Item names ------------------------------------------------------------ */

static const char* const sItemNames[] = {
	[0x01] = "Smith's Sword", [0x02] = "White Sword", [0x03] = "White Sword", [0x04] = "White Sword",
	[0x05] = "Ice Rod", [0x06] = "Four Sword", [0x07] = "Bombs", [0x08] = "Remote Bombs", [0x09] = "Bow",
	[0x0a] = "Light Arrows", [0x0b] = "Boomerang", [0x0c] = "Magic Boomerang", [0x0d] = "Shield",
	[0x0e] = "Mirror Shield", [0x0f] = "Lantern", [0x10] = "Lantern (lit)", [0x11] = "Gust Jar",
	[0x12] = "Pacci Cane", [0x13] = "Mole Mitts", [0x14] = "Roc's Cape", [0x15] = "Pegasus Boots",
	[0x16] = "Fire Rod", [0x17] = "Ocarina of Wind", [0x18] = "Quake Medallion", [0x19] = "Ether Medallion",
	[0x1a] = "Bombos Medallion", [0x1c] = "Bottle", [0x1d] = "Bottle", [0x1e] = "Bottle", [0x1f] = "Bottle",
	[0x20] = "Empty Bottle", [0x21] = "Lon Lon Butter", [0x22] = "Lon Lon Milk", [0x23] = "Half Milk",
	[0x24] = "Red Potion", [0x25] = "Blue Potion", [0x26] = "Water", [0x27] = "Mineral Water",
	[0x28] = "Bottled Fairy", [0x29] = "Red Picolyte", [0x2a] = "Orange Picolyte", [0x2b] = "Yellow Picolyte",
	[0x2c] = "Green Picolyte", [0x2d] = "Blue Picolyte", [0x2e] = "White Picolyte", [0x2f] = "Nayru Charm",
	[0x30] = "Farore Charm", [0x31] = "Din Charm", [0x32] = "Magic Mirror", [0x33] = "Moon Pearl",
	[0x34] = "Sword (quest)", [0x35] = "Broken Picori Blade", [0x36] = "Dog Food", [0x37] = "Lon Lon Key",
	[0x38] = "Wake-Up Mushroom", [0x39] = "Red Book", [0x3a] = "Green Book", [0x3b] = "Blue Book",
	[0x3c] = "Graveyard Key", [0x3d] = "Tingle Trophy", [0x3e] = "Carlov Medal", [0x3f] = "Mysterious Shells",
	[0x40] = "Earth Element", [0x41] = "Fire Element", [0x42] = "Water Element", [0x43] = "Wind Element",
	[0x44] = "Grip Ring", [0x45] = "Power Bracelets", [0x46] = "Flippers", [0x47] = "Map",
	[0x48] = "Spin Attack", [0x49] = "Roll Attack", [0x4a] = "Dash Attack", [0x4b] = "Rock Breaker",
	[0x4c] = "Sword Beam", [0x4d] = "Great Spin", [0x4e] = "Down Thrust", [0x4f] = "Peril Beam",
	[0x67] = "Kinstone Bag", [0x70] = "Bow Butterfly", [0x71] = "Digging Butterfly", [0x72] = "Swim Butterfly",
	[0x73] = "Fast Spin Scroll", [0x74] = "Fast Split Scroll", [0x75] = "Long Spin Scroll", [0x76] = "Hookshot",
	[0x77] = "Magic Hammer", [0x78] = "Cane of Byrna", [0x79] = "Magic Powder", [0x7a] = "Cane of Somaria",
	[0x7b] = "Revive Fairy",
};

static const char* itemName(unsigned item) {
	if (item < sizeof(sItemNames) / sizeof(sItemNames[0]) && sItemNames[item])
		return sItemNames[item];
	return "Item";
}

/** Items the partner gets too when one player finds them: equipment, key items, skills. */
static int isSharedItem(unsigned item) {
	if (item >= 0x01 && item <= 0x1f)
		return item != 0x10 && item != 0x1b; // not the lit lantern, not "pick up"
	if (item >= 0x32 && item <= 0x3e)
		return 1; // Magic Mirror, Moon Pearl, quest items
	if (item >= 0x40 && item <= 0x4f)
		return 1; // Elements, Grip Ring, Bracelets, Flippers, map, sword skills
	if (item == 0x67)
		return 1; // Kinstone Bag
	if (item >= 0x7c && item <= 0x81)
		return 1; // Book of Mudora, Magic Cape, Bug Net, Mushroom, Shovel, Titan's Mitt
	return item >= 0x70 && item <= 0x7a; // butterflies, scrolls, A Link to the Past items (not the Revive Fairy)
}

/* ---- Starting the games --------------------------------------------------- */

static int startPlayer(struct Player* pl, const char* rom, const char* save) {
	pl->core = mCoreFind(rom);
	if (!pl->core || !pl->core->init(pl->core)) {
		fprintf(stderr, "Could not start the game core for %s\n", rom);
		return 0;
	}
	mCoreInitConfig(pl->core, NULL);
	pl->video = calloc(GBA_W * GBA_H, sizeof(color_t));
	pl->overlay = calloc(OV_W * OV_H, sizeof(uint32_t));
	pl->core->setVideoBuffer(pl->core, pl->video, GBA_W);
	pl->core->setAudioBufferSize(pl->core, 2048);
	blip_set_rates(pl->core->getAudioChannel(pl->core, 0), pl->core->frequency(pl->core), AUDIO_RATE);
	blip_set_rates(pl->core->getAudioChannel(pl->core, 1), pl->core->frequency(pl->core), AUDIO_RATE);
	if (!mCoreLoadFile(pl->core, rom)) {
		fprintf(stderr, "Could not load %s\n", rom);
		return 0;
	}
	mCoreLoadSaveFile(pl->core, save, false);
	pl->core->reset(pl->core);
	return 1;
}

/* ---- State exchange ------------------------------------------------------- */

static void exchangeState(struct Player* a, struct Player* b) {
	uint8_t sa[LINK_SYNC_SIZE], sb[LINK_SYNC_SIZE];
	for (int i = 0; i < LINK_SYNC_SIZE; i++) {
		sa[i] = r8(a, LINK_SELF + i);
		sb[i] = r8(b, LINK_SELF + i);
	}
	for (int i = 0; i < LINK_SYNC_SIZE; i++) {
		w8(a, LINK_PARTNER + i, sb[i]);
		w8(b, LINK_PARTNER + i, sa[i]);
	}
	w8(a, LINK_HOST_CONNECTED, 1);
	w8(b, LINK_HOST_CONNECTED, 1);
	w8(a, LINK_PLAYER_INDEX, 0);
	w8(b, LINK_PLAYER_INDEX, 1);

	// going down and reviving, and where each one stands for a regroup
	struct Player* pair[2] = { a, b };
	for (int i = 0; i < 2; i++) {
		struct Player* self = pair[i];
		struct Player* other = pair[i ^ 1];
		int ready = coopReady(self), otherIn = inGame(other);
		if (!ready)
			continue;
		w8(self, COOP + C_PARTNER_IN_GAME, otherIn);
		w8(self, COOP + C_PARTNER_DOWNED, otherIn && r8(other, COOP + C_DOWNED));
		w8(self, COOP + C_PARTNER_REVIVE_SEQ, otherIn ? r8(other, COOP + C_REVIVE_SEQ) : 0);
		w8(self, COOP + C_PARTNER_ROOM_OK, otherIn && r8(other, LINK_SELF + SYNC_ACTIVE));
		w16(self, COOP + C_PARTNER_ROOM_X, r16(other, COOP + C_ROOM_X));
		w16(self, COOP + C_PARTNER_ROOM_Y, r16(other, COOP + C_ROOM_Y));
		w8(self, COOP + C_PARTNER_LAYER, r8(other, COOP + C_LAYER));
		// carrying and throwing, giving hearts, puzzle pieces
		w8(self, COOP + C_PARTNER_CARRY_STATE, otherIn ? r8(other, COOP + C_CARRY_STATE) : 0);
		w8(self, COOP + C_PARTNER_CARRY_SEQ, r8(other, COOP + C_CARRY_SEQ));
		w16(self, COOP + C_PARTNER_CARRY_X, r16(other, COOP + C_CARRY_X));
		w16(self, COOP + C_PARTNER_CARRY_Y, r16(other, COOP + C_CARRY_Y));
		w8(self, COOP + C_PARTNER_CARRY_Z, r8(other, COOP + C_CARRY_Z));
		w8(self, COOP + C_PARTNER_LIFTABLE, otherIn && r8(other, COOP + C_LIFTABLE));
		w8(self, COOP + C_PARTNER_HEART_SEQ, r8(other, COOP + C_HEART_SEQ));
		if (otherIn) {
			uint32_t st = saveAddr(other) + S_STATS;
			w8(self, COOP + C_PARTNER_HEALTH, r8(other, st + ST_HEALTH));
			w8(self, COOP + C_PARTNER_MAX_HEALTH, r8(other, st + ST_MAX_HEALTH));
		}
		w8(self, COOP + C_PARTNER_PIECES_LIT, otherIn ? r8(other, COOP + C_PIECES_LIT) : 0);
	}
}

/* ---- Shared progress -------------------------------------------------------- */

static int heartsTotal(struct Player* pl) {
	uint32_t st = saveAddr(pl) + S_STATS;
	return r8(pl, st + ST_MAX_HEALTH) / 8 * 4 + r8(pl, st + ST_HEART_PIECES) % 4;
}

static void setHeartsTotal(struct Player* pl, int total) {
	uint32_t st = saveAddr(pl) + S_STATS;
	int oldMax = r8(pl, st + ST_MAX_HEALTH);
	int max = total / 4 * 8;
	if (max > 160)
		max = 160;
	if (max < 8)
		max = 8;
	w8(pl, st + ST_MAX_HEALTH, max);
	w8(pl, st + ST_HEART_PIECES, total % 4);
	if (max > oldMax) {
		// a new heart container comes with a full heart
		int h = r8(pl, st + ST_HEALTH) + (max - oldMax);
		w8(pl, st + ST_HEALTH, h > max ? max : h);
	}
}

static void readBits(struct Player* pl, uint8_t* out) {
	uint32_t base = saveAddr(pl);
	int n = 0;
	for (size_t r = 0; r < sizeof(sSharedBits) / sizeof(sSharedBits[0]); r++)
		for (uint32_t i = 0; i < sSharedBits[r].size; i++)
			out[n++] = r8(pl, base + sSharedBits[r].offset + i);
}

static void writeBits(struct Player* pl, const uint8_t* now, const uint8_t* want) {
	uint32_t base = saveAddr(pl);
	int n = 0;
	for (size_t r = 0; r < sizeof(sSharedBits) / sizeof(sSharedBits[0]); r++)
		for (uint32_t i = 0; i < sSharedBits[r].size; i++, n++)
			if (now[n] != want[n])
				w8(pl, base + sSharedBits[r].offset + i, want[n]);
}

static void readKeys(struct Player* pl, uint8_t* out) {
	uint32_t base = saveAddr(pl);
	int n = 0;
	for (size_t r = 0; r < sizeof(sSharedKeys) / sizeof(sSharedKeys[0]); r++)
		for (uint32_t i = 0; i < sSharedKeys[r].size; i++)
			out[n++] = r8(pl, base + sSharedKeys[r].offset + i);
}

static void writeKeys(struct Player* pl, const uint8_t* v) {
	uint32_t base = saveAddr(pl);
	int n = 0;
	for (size_t r = 0; r < sizeof(sSharedKeys) / sizeof(sSharedKeys[0]); r++)
		for (uint32_t i = 0; i < sSharedKeys[r].size; i++, n++)
			if (r8(pl, base + sSharedKeys[r].offset + i) != v[n])
				w8(pl, base + sSharedKeys[r].offset + i, v[n]);
}

static void readInventory(struct Player* pl, uint8_t* out) {
	uint32_t base = saveAddr(pl) + S_INVENTORY;
	for (int i = 0; i < INVENTORY_BYTES; i++)
		out[i] = r8(pl, base + i);
}

static int owns(const uint8_t* inv, unsigned item) {
	return (inv[item / 4] >> ((item & 3) * 2)) & 3;
}

static void queueGive(struct Player* to, unsigned item) {
	for (int i = 0; i < to->giveCount; i++)
		if (to->giveQueue[i] == item)
			return;
	if (to->giveCount < (int) sizeof(to->giveQueue))
		to->giveQueue[to->giveCount++] = item;
}

/** Hands queued items to the game one at a time, through gBlendCoop.shareGive. */
static void deliverGives(struct Player* pl, int partnerNumber) {
	if (r8(pl, COOP + C_SHARE_GIVE) != 0)
		return; // the game has not taken the last one yet
	if (pl->giveWaiting) {
		pl->giveWaiting = 0;
		toast(pl, "Player %d found the %s. You got it too!", partnerNumber, itemName(pl->giveQueue[0]));
		memmove(pl->giveQueue, pl->giveQueue + 1, --pl->giveCount);
	}
	if (pl->giveCount > 0 && r8(pl, LINK_SELF + SYNC_ACTIVE)) {
		w8(pl, COOP + C_SHARE_GIVE, pl->giveQueue[0]);
		pl->giveWaiting = 1;
	}
}

/**
 * Shares progress between the two games. What each player has done since the
 * last look is copied into the other game: flags (chests, doors, bosses, story),
 * small keys found and used, heart containers and pieces, wallet, bomb bag and
 * quiver sizes, and the equipment and key items they found. Hearts, Rupees,
 * bombs, arrows, bottles' contents and Revive Fairies stay each player's own.
 * When a game (re)loads its file, the two are first merged.
 */
static void syncProgress(struct Player* a, struct Player* b) {
	struct Player* pair[2] = { a, b };
	uint8_t bits[2][SHARED_BIT_BYTES], keys[2][SHARED_KEY_BYTES], inv[2][INVENTORY_BYTES];
	if (!inGame(a) || !inGame(b) || !r8(a, LINK_SELF + SYNC_ACTIVE) || !r8(b, LINK_SELF + SYNC_ACTIVE)) {
		// out of play (title, game over, a cutscene that hides Link): merge again on the way back
		if (!inGame(a))
			a->synced = 0;
		if (!inGame(b))
			b->synced = 0;
		return;
	}
	for (int i = 0; i < 2; i++) {
		readBits(pair[i], bits[i]);
		readKeys(pair[i], keys[i]);
		readInventory(pair[i], inv[i]);
	}
	if (!a->synced || !b->synced) {
		// merge: every bit either game has, the larger key count, the larger heart total
		uint8_t want[SHARED_BIT_BYTES], wantKeys[SHARED_KEY_BYTES];
		for (int n = 0; n < SHARED_BIT_BYTES; n++)
			want[n] = bits[0][n] | bits[1][n];
		for (int n = 0; n < SHARED_KEY_BYTES; n++)
			wantKeys[n] = keys[0][n] > keys[1][n] ? keys[0][n] : keys[1][n];
		int hearts = heartsTotal(a) > heartsTotal(b) ? heartsTotal(a) : heartsTotal(b);
		for (int i = 0; i < 2; i++) {
			struct Player* pl = pair[i];
			writeBits(pl, bits[i], want);
			writeKeys(pl, wantKeys);
			if (heartsTotal(pl) != hearts)
				setHeartsTotal(pl, hearts);
			memcpy(pl->snapBits, want, sizeof(want));
			memcpy(pl->snapKeys, wantKeys, sizeof(wantKeys));
			pl->snapHearts = hearts;
			memset(pl->snapInv, 0, sizeof(pl->snapInv)); // so every item either owns is shared below
			pl->synced = 1;
		}
		for (int i = 0; i < 2; i++)
			readBits(pair[i], bits[i]);
	}
	// flags: bits newly set in one game are set in the other
	for (int i = 0; i < 2; i++) {
		struct Player* self = pair[i];
		struct Player* other = pair[i ^ 1];
		uint8_t want[SHARED_BIT_BYTES];
		for (int n = 0; n < SHARED_BIT_BYTES; n++)
			want[n] = bits[i ^ 1][n] | (bits[i][n] & ~self->snapBits[n]);
		(void) other;
		memcpy(bits[i ^ 1], want, sizeof(want));
	}
	for (int i = 0; i < 2; i++) {
		struct Player* other = pair[i ^ 1];
		uint8_t now[SHARED_BIT_BYTES];
		readBits(other, now);
		writeBits(other, now, bits[i ^ 1]);
	}
	for (int i = 0; i < 2; i++)
		memcpy(pair[i]->snapBits, bits[i], SHARED_BIT_BYTES);
	// small keys: what one player picks up or spends, the other does too
	{
		uint8_t out[2][SHARED_KEY_BYTES];
		for (int n = 0; n < SHARED_KEY_BYTES; n++) {
			int dA = keys[0][n] - a->snapKeys[n];
			int dB = keys[1][n] - b->snapKeys[n];
			int va = keys[0][n] + dB, vb = keys[1][n] + dA;
			out[0][n] = va < 0 ? 0 : va > 255 ? 255 : va;
			out[1][n] = vb < 0 ? 0 : vb > 255 ? 255 : vb;
		}
		for (int i = 0; i < 2; i++) {
			writeKeys(pair[i], out[i]);
			memcpy(pair[i]->snapKeys, out[i], SHARED_KEY_BYTES);
		}
	}
	// heart containers and pieces
	{
		int ha = heartsTotal(a), hb = heartsTotal(b);
		int dA = ha - a->snapHearts, dB = hb - b->snapHearts;
		if (dB > 0)
			setHeartsTotal(a, ha + dB);
		if (dA > 0)
			setHeartsTotal(b, hb + dA);
		a->snapHearts = heartsTotal(a);
		b->snapHearts = heartsTotal(b);
	}
	// wallet, bomb bag and quiver: the bigger one
	{
		static const int stats[] = { ST_WALLET, ST_BOMB_BAG, ST_QUIVER };
		for (int k = 0; k < 3; k++) {
			uint32_t aa = saveAddr(a) + S_STATS + stats[k], bb = saveAddr(b) + S_STATS + stats[k];
			int va = r8(a, aa), vb = r8(b, bb);
			if (va > vb && va <= 3)
				w8(b, bb, va);
			else if (vb > va && vb <= 3)
				w8(a, aa, vb);
		}
	}
	// items: what one player newly owns, the other is given
	for (int i = 0; i < 2; i++) {
		struct Player* self = pair[i];
		struct Player* other = pair[i ^ 1];
		for (unsigned item = 1; item < INVENTORY_BYTES * 4; item++)
			if (isSharedItem(item) && owns(inv[i], item) && !owns(self->snapInv, item) && !owns(inv[i ^ 1], item))
				queueGive(other, item);
		memcpy(self->snapInv, inv[i], INVENTORY_BYTES);
	}
	deliverGives(a, 2);
	deliverGives(b, 1);
}

/* ---- Drawing the overlay ---------------------------------------------------- */

static uint32_t rgba(int r, int g, int b, int a) { return (uint32_t) a << 24 | (uint32_t) b << 16 | (uint32_t) g << 8 | r; }

static void blendPixel(uint32_t* d, uint32_t c, int alpha) {
	int a = ((c >> 24) & 0xff) * alpha / 255;
	if (a <= 0)
		return;
	uint32_t o = *d;
	int oa = (o >> 24) & 0xff;
	int na = a + oa * (255 - a) / 255;
	if (na <= 0)
		return;
	int r = ((c & 0xff) * a + (o & 0xff) * oa * (255 - a) / 255) / na;
	int g = (((c >> 8) & 0xff) * a + ((o >> 8) & 0xff) * oa * (255 - a) / 255) / na;
	int b = (((c >> 16) & 0xff) * a + ((o >> 16) & 0xff) * oa * (255 - a) / 255) / na;
	*d = rgba(r, g, b, na);
}

static void ovRect(struct Player* pl, int x, int y, int w, int h, uint32_t c) {
	for (int yy = y; yy < y + h; yy++)
		for (int xx = x; xx < x + w; xx++)
			if (xx >= 0 && yy >= 0 && xx < OV_W && yy < OV_H)
				blendPixel(&pl->overlay[yy * OV_W + xx], c, 255);
}

static void ovFrame(struct Player* pl, int x, int y, int w, int h, int t, uint32_t c) {
	ovRect(pl, x, y, w, t, c);
	ovRect(pl, x, y + h - t, w, t, c);
	ovRect(pl, x, y + t, t, h - 2 * t, c);
	ovRect(pl, x + w - t, y + t, t, h - 2 * t, c);
}

/** A panel in the blend's style: dark plum glass, a gold rim and a dark outline. */
static void ovPanel(struct Player* pl, int x, int y, int w, int h, int alpha) {
	ovRect(pl, x, y, w, h, rgba(28, 20, 34, alpha));
	ovFrame(pl, x, y, w, h, 2, rgba(36, 24, 28, 255));
	ovFrame(pl, x + 2, y + 2, w - 4, h - 4, 2, rgba(216, 152, 40, 255));
	ovRect(pl, x + 4, y + 4, w - 8, 1, rgba(255, 226, 112, 120));
}

static int textWidth(const char* s) {
	int w = 0;
	for (; *s; s++)
		if (*s >= 32 && *s < 127)
			w += sGlyphs[*s - 32].advance;
	return w;
}

static void ovGlyphs(struct Player* pl, int x, int y, const char* s, uint32_t c) {
	for (; *s; s++) {
		if (*s < 32 || *s >= 127)
			continue;
		const struct Glyph* g = &sGlyphs[*s - 32];
		const unsigned char* px = &sGlyphData[g->offset];
		for (int yy = 0; yy < FONT_HEIGHT; yy++)
			for (int xx = 0; xx < g->width; xx++) {
				int X = x + xx - 1, Y = y + yy;
				if (px[yy * g->width + xx] && X >= 0 && Y >= 0 && X < OV_W && Y < OV_H)
					blendPixel(&pl->overlay[Y * OV_W + X], c, px[yy * g->width + xx]);
			}
		x += g->advance;
	}
}

/** Text with a dark outline, readable over the game. */
static void ovText(struct Player* pl, int x, int y, const char* s, uint32_t c) {
	static const int d[8][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
	for (int i = 0; i < 8; i++)
		ovGlyphs(pl, x + d[i][0], y + d[i][1], s, rgba(24, 16, 20, 230));
	ovGlyphs(pl, x, y, s, c);
}

static void ovTextCentered(struct Player* pl, int cx, int y, const char* s, uint32_t c) {
	ovText(pl, cx - textWidth(s) / 2, y, s, c);
}

#define WHITE rgba(255, 255, 255, 255)
#define GOLD rgba(255, 226, 112, 255)
#define PINK rgba(255, 150, 196, 255)
#define RED rgba(255, 96, 96, 255)
#define GREY rgba(190, 190, 200, 255)

/** Draws one item picture straight from the player's game: the item sprite's frame, in its colors. */
static void ovItemPicture(struct Player* pl, int x, int y, int frame, int scale) {
	uint32_t sprite = r32(pl, COOP + C_ITEM_FRAMES);
	uint32_t lists = r32(pl, COOP + C_FRAME_OBJ_LISTS);
	if ((sprite >> 24) != 8 || (lists >> 24) != 8)
		return;
	uint32_t frames = r32(pl, sprite + 4), gfx = r32(pl, sprite + 8);
	uint16_t firstTile = r16(pl, frames + frame * 4 + 2);
	uint32_t first = r32(pl, lists + ITEM_SPRITE * 4);
	uint32_t off = r32(pl, lists + first + frame * 4);
	int count = r8(pl, lists + off);
	int base = r8(pl, COOP + C_ITEM_PALETTE);
	static const int dims[3][4][2] = {
		{ { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } },
		{ { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } },
		{ { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } },
	};
	for (int o = count - 1; o >= 0; o--) {
		uint32_t at = lists + off + 1 + o * 5;
		int ox = (int8_t) r8(pl, at), oy = (int8_t) r8(pl, at + 1), b3 = r8(pl, at + 2);
		int tile = r8(pl, at + 3) | r8(pl, at + 4) << 8;
		int shape = b3 >> 6, size = (b3 >> 4) & 3;
		if (shape > 2)
			continue;
		int w = dims[shape][size][0], h = dims[shape][size][1];
		int pal = (base + ((b3 & 1) ? (tile >> 12) : 0)) & 15;
		int hflip = b3 & 4, vflip = b3 & 8;
		uint32_t palAddr = 0x05000200 + pal * 32;
		for (int py = 0; py < h; py++)
			for (int px = 0; px < w; px++) {
				int sx = hflip ? w - 1 - px : px, sy = vflip ? h - 1 - py : py;
				int t = (tile & 0x3ff) + (sy / 8) * (w / 8) + sx / 8;
				uint32_t addr = gfx + (firstTile + t) * 32 + (sy & 7) * 4 + (sx & 7) / 2;
				int ci = (r8(pl, addr) >> ((sx & 1) * 4)) & 15;
				if (!ci)
					continue;
				uint16_t c = r16(pl, palAddr + ci * 2);
				uint32_t col = rgba((c & 31) * 255 / 31, ((c >> 5) & 31) * 255 / 31, ((c >> 10) & 31) * 255 / 31, 255);
				// pictures stand on (0, 0); centre a 16x16 item on (x, y)
				int X = x + (ox + 8 + px) * scale, Y = y + (oy + 14 + py) * scale;
				ovRect(pl, X, Y, scale, scale, col);
			}
	}
}

static void ovRevivePicture(struct Player* pl, int x, int y, int scale) {
	static const int beat[] = { 0, 1, 2, 1 };
	const unsigned int* art = sReviveArt[beat[(tick >> 3) & 3]];
	for (int py = 0; py < 16; py++)
		for (int px = 0; px < 16; px++)
			if (art[py * 16 + px] >> 24)
				ovRect(pl, x + px * scale, y + py * scale, scale, scale, art[py * 16 + px]);
}

/** A small heart, filled by quarters (the game's 8 health points to a heart). */
static void ovHeart(struct Player* pl, int x, int y, int fill) {
	static const char* const shape[] = {
		".XX.XX.",
		"XXXXXXX",
		"XXXXXXX",
		".XXXXX.",
		"..XXX..",
		"...X...",
	};
	for (int yy = 0; yy < 6; yy++)
		for (int xx = 0; xx < 7; xx++)
			if (shape[yy][xx] == 'X') {
				int quarter = xx < 4 ? (yy < 3 ? 1 : 2) : (yy < 3 ? 4 : 3);
				uint32_t c = quarter <= fill ? rgba(232, 40, 56, 255) : rgba(70, 40, 50, 220);
				ovRect(pl, x + xx * 2, y + yy * 2, 2, 2, c);
			}
}

/* ---- The quick inventory ---------------------------------------------------- */

#define GRID_COLS 6
#define CELL_W 64
#define CELL_H 42
#define PANEL_X 16
#define PANEL_Y 8
#define PANEL_W (OV_W - 32)
#define PANEL_H (OV_H - 16)
#define GRID_X (PANEL_X + 32)
#define GRID_Y (PANEL_Y + 34)

static int menuCount(struct Player* pl) {
	int n = r8(pl, COOP + C_MENU_COUNT);
	return n > MENU_MAX ? MENU_MAX : n;
}

/** Entries after the items: the Revive Fairies (shown, not equipped) and, in co-op, the regroup. */
static int menuSpecials(void) { return players == 2 ? 2 : 1; }

static int giftKind(struct Player* pl, int entry);

static void drawMenu(struct Player* pl) {
	int n = menuCount(pl);
	char line[96];
	ovPanel(pl, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 214);
	snprintf(line, sizeof(line), "Player %d  -  Items", pl->index + 1);
	ovText(pl, PANEL_X + 16, PANEL_Y + 12, line, GOLD);
	// counts, top right
	uint32_t st = saveAddr(pl) + S_STATS;
	snprintf(line, sizeof(line), "Rupees %d   Bombs %d   Arrows %d", r16(pl, st + ST_RUPEES), r8(pl, COOP + C_BOMBS),
	         r8(pl, COOP + C_ARROWS));
	ovText(pl, PANEL_X + PANEL_W - 16 - textWidth(line), PANEL_Y + 12, line, GREY);
	int equippedA = r8(pl, COOP + C_EQUIPPED_A), equippedB = r8(pl, COOP + C_EQUIPPED_B);
	if (n == 0)
		ovText(pl, GRID_X, GRID_Y + 16, "No items yet.", GREY);
	for (int i = 0; i < n; i++) {
		int cx = GRID_X + (i % GRID_COLS) * CELL_W, cy = GRID_Y + (i / GRID_COLS) * CELL_H;
		ovRect(pl, cx + 4, cy + 2, CELL_W - 8, CELL_H - 6, rgba(255, 255, 255, 22));
		ovItemPicture(pl, cx + CELL_W / 2 - 16, cy + 3, r8(pl, COOP + C_MENU_FRAME + i), 2);
		if (equippedA == i + 1 || equippedB == i + 1) {
			const char* tag = equippedA == i + 1 ? "A" : "B";
			ovRect(pl, cx + CELL_W - 22, cy + CELL_H - 22, 16, 16, rgba(36, 24, 28, 255));
			ovRect(pl, cx + CELL_W - 21, cy + CELL_H - 21, 14, 14, equippedA == i + 1 ? rgba(200, 60, 60, 255) : rgba(60, 110, 210, 255));
			ovText(pl, cx + CELL_W - 18, cy + CELL_H - 23, tag, WHITE);
		}
		if (pl->cursor == i)
			ovFrame(pl, cx + 2, cy, CELL_W - 4, CELL_H - 2, 3, GOLD);
	}
	// the Revive Fairies and the regroup, under the grid
	int sy = GRID_Y + 4 * CELL_H + 4;
	int fairies = r8(pl, COOP + C_FAIRIES);
	ovRect(pl, GRID_X + 4, sy, 180, 36, rgba(255, 255, 255, 22));
	ovRevivePicture(pl, GRID_X + 10, sy + 2, 2);
	snprintf(line, sizeof(line), "Revive Fairy x%d", fairies);
	ovText(pl, GRID_X + 50, sy + 10, line, fairies ? PINK : GREY);
	if (pl->cursor == n)
		ovFrame(pl, GRID_X + 2, sy - 2, 184, 40, 3, GOLD);
	if (players == 2) {
		ovRect(pl, GRID_X + 196, sy, 180, 36, rgba(255, 255, 255, 22));
		snprintf(line, sizeof(line), "Go to Player %d", (pl->index ^ 1) + 1);
		ovText(pl, GRID_X + 214, sy + 10, line, WHITE);
		if (pl->cursor == n + 1)
			ovFrame(pl, GRID_X + 194, sy - 2, 184, 40, 3, GOLD);
	}
	// what the cursor is on, and what the buttons do
	int by = PANEL_Y + PANEL_H - 50;
	ovRect(pl, PANEL_X + 8, by - 6, PANEL_W - 16, 1, rgba(216, 152, 40, 160));
	if (pl->cursor < n) {
		int kind = giftKind(pl, pl->cursor);
		ovText(pl, PANEL_X + 16, by, itemName(r8(pl, COOP + C_MENU_ITEM + pl->cursor)), WHITE);
		if (kind) {
			snprintf(line, sizeof(line), "A / B: equip.  L: give Player %d 5 %s.", (pl->index ^ 1) + 1,
			         kind == 2 ? "bombs" : "arrows");
			ovText(pl, PANEL_X + 16, by + 20, line, GREY);
		} else {
			ovText(pl, PANEL_X + 16, by + 20, "A / B: put it on that button.  Start: close.", GREY);
		}
	} else if (pl->cursor == n) {
		ovText(pl, PANEL_X + 16, by, "Revive Fairy: brings a fallen partner back.", WHITE);
		if (players == 2) {
			snprintf(line, sizeof(line), "Stand by them and press R.  L: give one to Player %d.", (pl->index ^ 1) + 1);
			ovText(pl, PANEL_X + 16, by + 20, line, GREY);
		} else {
			ovText(pl, PANEL_X + 16, by + 20, "Stand by them and press R. Sold at Stockwell's.", GREY);
		}
	} else {
		snprintf(line, sizeof(line), "Warp to where Player %d is.", (pl->index ^ 1) + 1);
		ovText(pl, PANEL_X + 16, by, line, WHITE);
		ovText(pl, PANEL_X + 16, by + 20, "A: go.  Start: close.", GREY);
	}
}

#define S_REVIVE_FAIRIES 0x1f /* gSave.fillerA[21] */
#define ST_BOMBS 0x04
#define ST_ARROWS 0x05
static const int sBombBagSizes[] = { 10, 30, 50, 99 };
static const int sQuiverSizes[] = { 30, 50, 70, 99 };

static int ownsItem(struct Player* pl, unsigned item) {
	return (r8(pl, saveAddr(pl) + S_INVENTORY + item / 4) >> ((item & 3) * 2)) & 3;
}

/** What L gives the partner from this menu entry: 1 Revive Fairy, 2 bombs, 3 arrows, 0 nothing. */
static int giftKind(struct Player* pl, int entry) {
	int n = menuCount(pl);
	if (players != 2)
		return 0;
	if (entry == n)
		return 1;
	if (entry < n) {
		int item = r8(pl, COOP + C_MENU_ITEM + entry);
		if (item == 0x07 || item == 0x08)
			return 2;
		if (item == 0x09 || item == 0x0a)
			return 3;
	}
	return 0;
}

/** L in the quick inventory: hand the partner a Revive Fairy, or 5 bombs or arrows. */
static void giveToPartner(struct Player* pl, int kind) {
	struct Player* to = &p[pl->index ^ 1];
	int other = to->index + 1;
	if (!inGame(to)) {
		toast(pl, "Player %d isn't playing right now.", other);
		return;
	}
	if (kind == 1) {
		uint32_t a = saveAddr(pl) + S_REVIVE_FAIRIES, b = saveAddr(to) + S_REVIVE_FAIRIES;
		if (r8(pl, a) == 0) {
			toast(pl, "You have no Revive Fairy to give.");
		} else if (r8(to, b) >= REVIVE_FAIRIES_MAX) {
			toast(pl, "Player %d can't carry more Revive Fairies.", other);
		} else {
			w8(pl, a, r8(pl, a) - 1);
			w8(to, b, r8(to, b) + 1);
			toast(pl, "You gave Player %d a Revive Fairy.", other);
			toast(to, "Player %d gave you a Revive Fairy!", pl->index + 1);
		}
		return;
	}
	{
		int stat = kind == 2 ? ST_BOMBS : ST_ARROWS;
		const char* what = kind == 2 ? "bombs" : "arrows";
		uint32_t a = saveAddr(pl) + S_STATS + stat, b = saveAddr(to) + S_STATS + stat;
		int type = r8(to, saveAddr(to) + S_STATS + (kind == 2 ? ST_BOMB_BAG : ST_QUIVER)) & 3;
		int cap = kind == 2 ? sBombBagSizes[type] : sQuiverSizes[type];
		int have = r8(pl, a), theirs = r8(to, b);
		int give = have < 5 ? have : 5;
		if (!ownsItem(to, kind == 2 ? 0x07 : 0x09)) {
			toast(pl, "Player %d has no %s yet.", other, kind == 2 ? "bomb bag" : "bow");
			return;
		}
		if (give > cap - theirs)
			give = cap - theirs;
		if (give <= 0) {
			if (have)
				toast(pl, "Player %d's %s are full.", other, what);
			else
				toast(pl, "You have no %s to give.", what);
			return;
		}
		w8(pl, a, have - give);
		w8(to, b, theirs + give);
		toast(pl, "You gave Player %d %d %s.", other, give, what);
		toast(to, "Player %d gave you %d %s!", pl->index + 1, give, what);
	}
}

/** Moves the cursor; returns the keys the game should see (none while the menu is open). */
static uint16_t menuInput(struct Player* pl, uint16_t keys, int togglePressed) {
	uint16_t pressed = keys & ~pl->prevKeys;
	pl->prevKeys = keys;
	pl->heldAtClose &= keys;
	if (!pl->menuOpen) {
		if (togglePressed && inGame(pl) && r8(pl, LINK_SELF + SYNC_ACTIVE) && !r8(pl, COOP + C_DOWNED)) {
			pl->menuOpen = 1;
			if (pl->cursor >= menuCount(pl) + menuSpecials())
				pl->cursor = 0;
			w8(pl, COOP + C_QUICK_MENU, 1);
			return 0;
		}
		return keys & ~pl->heldAtClose;
	}
	if (togglePressed || (pressed & GBA_START) || !inGame(pl) || r8(pl, COOP + C_DOWNED)) {
		pl->menuOpen = 0;
		w8(pl, COOP + C_QUICK_MENU, 0);
		pl->heldAtClose = keys; // the closing Start must not open the pause menu
		return 0;
	}
	// held directions repeat after a moment
	uint16_t dirs = keys & (GBA_UP | GBA_DOWN | GBA_LEFT | GBA_RIGHT);
	uint16_t move = pressed & dirs;
	if (dirs && !move) {
		if (++pl->repeat > 18 && (pl->repeat & 3) == 0)
			move = dirs;
	} else {
		pl->repeat = 0;
	}
	int n = menuCount(pl), total = n + menuSpecials();
	int c = pl->cursor;
	if (move & GBA_RIGHT)
		c++;
	if (move & GBA_LEFT)
		c--;
	if (move & GBA_DOWN)
		c = c < n ? (c + GRID_COLS < n ? c + GRID_COLS : n) : c;
	if (move & GBA_UP)
		c = c >= n ? (n > 0 ? n - 1 - (n - 1) % GRID_COLS : 0) : (c >= GRID_COLS ? c - GRID_COLS : c);
	if (c < 0)
		c = total - 1;
	if (c >= total)
		c = 0;
	pl->cursor = c;
	if ((pressed & GBA_L) && giftKind(pl, c)) {
		giveToPartner(pl, giftKind(pl, c));
		return 0;
	}
	if (pressed & (GBA_A | GBA_B)) {
		if (c < n) {
			w8(pl, COOP + C_QUICK_EQUIP_SLOT, (pressed & GBA_A) ? 0 : 1);
			w8(pl, COOP + C_QUICK_EQUIP, c + 1);
		} else if (c == n + 1 && (pressed & GBA_A)) {
			w8(pl, COOP + C_REGROUP_FAILED, 0);
			w8(pl, COOP + C_REGROUP, 1);
			pl->menuOpen = 0;
			w8(pl, COOP + C_QUICK_MENU, 0);
			pl->heldAtClose = keys;
		}
	}
	return 0;
}

/* ---- Prompts ---------------------------------------------------------------- */

static void drawBanner(struct Player* pl, int y, const char* l1, uint32_t c1, const char* l2) {
	int w = textWidth(l1);
	if (l2 && textWidth(l2) > w)
		w = textWidth(l2);
	int h = l2 ? 48 : 30;
	ovPanel(pl, OV_W / 2 - w / 2 - 18, y, w + 36, h, 200);
	ovTextCentered(pl, OV_W / 2, y + 8, l1, c1);
	if (l2)
		ovTextCentered(pl, OV_W / 2, y + 26, l2, GREY);
}

static void drawPartnerHearts(struct Player* pl, struct Player* partner) {
	uint32_t st = saveAddr(partner) + S_STATS;
	int health = r8(partner, st + ST_HEALTH), max = r8(partner, st + ST_MAX_HEALTH);
	int hearts = max / 8;
	if (hearts > 20)
		hearts = 20;
	char label[16];
	snprintf(label, sizeof(label), "P%d", partner->index + 1);
	// bottom left, clear of the game's own hearts, items and Rupees
	int w = 30 + (hearts > 10 ? 10 : hearts) * 16;
	int x = 8, y = OV_H - (hearts > 10 ? 36 : 22);
	ovRect(pl, x - 4, y - 4, w + 4, hearts > 10 ? 38 : 24, rgba(28, 20, 34, 150));
	ovText(pl, x, y - 1, label, partner->index ? rgba(150, 200, 255, 255) : rgba(140, 230, 140, 255));
	for (int i = 0; i < hearts; i++) {
		int fill = health - i * 8;
		fill = fill <= 0 ? 0 : fill >= 8 ? 4 : fill / 2;
		ovHeart(pl, x + 26 + (i % 10) * 16, y + 2 + (i / 10) * 14, fill);
	}
}

static void buildOverlay(struct Player* pl, struct Player* partner) {
	memset(pl->overlay, 0, OV_W * OV_H * sizeof(uint32_t));
	if (!inGame(pl))
		return;
	char l1[96], l2[96];
	int other = partner ? partner->index + 1 : 0;
	int page = r8(pl, COOP + C_MENU_PAGE);
	if (page) {
		// the game's pause menu: no co-op prompts over it; on the Items page, say how to switch pages
		if (page >= 2) {
			const char* hint = page == 2 ? "Select: A Link to the Past items" : "Select: Minish Cap items";
			int w = textWidth(hint);
			ovPanel(pl, 292 - w / 2 - 10, 46, w + 20, 26, 200);
			ovTextCentered(pl, 292, 51, hint, GOLD);
		}
		if (pl->toast.frames > 0) {
			pl->toast.frames--;
			drawBanner(pl, 96, pl->toast.text, GOLD, NULL);
		}
		return;
	}
	if (partner && inGame(partner) && !pl->menuOpen && r8(pl, LINK_SELF + SYNC_ACTIVE) && !r8(pl, COOP + C_MESSAGE_OPEN))
		drawPartnerHearts(pl, partner);
	if (r8(pl, COOP + C_DOWNED)) {
		int t = r8(pl, COOP + C_GIVE_UP_TIMER);
		snprintf(l1, sizeof(l1), "You're down! Player %d can revive you.", other);
		drawBanner(pl, 70, l1, RED, "They need a Revive Fairy. Hold A + B to give up.");
		if (t > 0) {
			ovRect(pl, OV_W / 2 - 100, 124, 200, 8, rgba(36, 24, 28, 230));
			ovRect(pl, OV_W / 2 - 98, 126, 196 * t / GIVE_UP_FRAMES, 4, RED);
		}
	} else if (partner && inGame(partner) && r8(partner, COOP + C_DOWNED) && !pl->menuOpen) {
		int fairies = r8(pl, COOP + C_FAIRIES);
		int here = r8(pl, LINK_SELF + SYNC_AREA) == r8(partner, LINK_SELF + SYNC_AREA) &&
		           r8(pl, LINK_SELF + SYNC_ROOM) == r8(partner, LINK_SELF + SYNC_ROOM) &&
		           r8(pl, LINK_SELF + SYNC_DARK_WORLD) == r8(partner, LINK_SELF + SYNC_DARK_WORLD);
		if (r8(pl, COOP + C_REVIVE_NEAR)) {
			if (fairies)
				snprintf(l1, sizeof(l1), "R: revive Player %d  (Revive Fairies: %d)", other, fairies);
			else
				snprintf(l1, sizeof(l1), "No Revive Fairy! Stockwell's shop sells them.");
			drawBanner(pl, OV_H - 80, l1, fairies ? PINK : RED, NULL);
		} else if (here) {
			snprintf(l1, sizeof(l1), "Player %d is down! Go to them and press R.", other);
			drawBanner(pl, 40, l1, RED, NULL);
		} else {
			snprintf(l1, sizeof(l1), "Player %d is down in another room!", other);
			snprintf(l2, sizeof(l2), "Quick inventory: \"Go to Player %d\" takes you there.", other);
			drawBanner(pl, 40, l1, RED, l2);
		}
	}
	if (partner && !pl->menuOpen && !r8(pl, COOP + C_DOWNED) && !r8(pl, COOP + C_MESSAGE_OPEN)) {
		int carry = r8(pl, COOP + C_CARRY_STATE);
		if (carry == 1) {
			snprintf(l1, sizeof(l1), "R or A: throw Player %d", other);
			drawBanner(pl, OV_H - 80, l1, GOLD, NULL);
		} else if (r8(pl, COOP + C_CARRIED) && r8(pl, COOP + C_PARTNER_CARRY_STATE) == 1) {
			snprintf(l1, sizeof(l1), "Player %d is carrying you! You can still swing your sword.", other);
			drawBanner(pl, OV_H - 80, l1, GOLD, NULL);
		} else if (inGame(partner) && r8(pl, LINK_SELF + SYNC_ACTIVE) && pl->hintFrames < 900 &&
		           r8(pl, LINK_SELF + SYNC_AREA) == r8(partner, LINK_SELF + SYNC_AREA) &&
		           r8(pl, LINK_SELF + SYNC_ROOM) == r8(partner, LINK_SELF + SYNC_ROOM) &&
		           r8(pl, COOP + C_PARTNER_LIFTABLE)) {
			int dx = (int16_t) (r16(pl, LINK_SELF + SYNC_X) - r16(partner, LINK_SELF + SYNC_X));
			int dy = (int16_t) (r16(pl, LINK_SELF + SYNC_Y) - r16(partner, LINK_SELF + SYNC_Y));
			if (dx > -24 && dx < 24 && dy > -24 && dy < 24) {
				pl->hintFrames++;
				snprintf(l1, sizeof(l1), "R: pick up Player %d     L + R: give a heart", other);
				drawBanner(pl, OV_H - 80, l1, WHITE, NULL);
			}
		}
	}
	if (r8(pl, COOP + C_REGROUP_FAILED)) {
		w8(pl, COOP + C_REGROUP_FAILED, 0);
		toast(pl, "Can't warp right now. Try again when you're free to move.");
	}
	if (pl->menuOpen)
		drawMenu(pl);
	if (pl->toast.frames > 0) {
		pl->toast.frames--;
		drawBanner(pl, pl->menuOpen ? OV_H - 46 : 96, pl->toast.text, GOLD, NULL);
	}
}

/* ---- One frame for both players ------------------------------------------- */

/** Runs one host frame: menus and input, the state exchange, both games, the overlays. */
static void hostFrame(const uint16_t keys[2], const int toggle[2]) {
	for (int i = 0; i < players; i++) {
		uint16_t k = menuInput(&p[i], keys[i], toggle[i]);
		p[i].core->setKeys(p[i].core, k);
	}
	if (players == 2) {
		exchangeState(&p[0], &p[1]);
		syncProgress(&p[0], &p[1]);
	}
	for (int i = 0; i < players; i++) {
		// the quick inventory runs this player's game at half speed
		if (!p[i].menuOpen || (tick & 1))
			p[i].core->runFrame(p[i].core);
	}
	for (int i = 0; i < players; i++)
		buildOverlay(&p[i], players == 2 ? &p[i ^ 1] : NULL);
	tick++;
}

/* ---- Input -------------------------------------------------------------------- */

static uint16_t padKeys(SDL_GameController* pad) {
	uint16_t k = 0;
	if (!pad)
		return 0;
	static const struct { SDL_GameControllerButton b; uint16_t k; } map[] = {
		{ SDL_CONTROLLER_BUTTON_A, GBA_B }, { SDL_CONTROLLER_BUTTON_B, GBA_A },
		{ SDL_CONTROLLER_BUTTON_X, GBA_B },
		{ SDL_CONTROLLER_BUTTON_LEFTSHOULDER, GBA_L }, { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, GBA_R },
		{ SDL_CONTROLLER_BUTTON_START, GBA_START }, { SDL_CONTROLLER_BUTTON_BACK, GBA_SELECT },
		{ SDL_CONTROLLER_BUTTON_DPAD_UP, GBA_UP }, { SDL_CONTROLLER_BUTTON_DPAD_DOWN, GBA_DOWN },
		{ SDL_CONTROLLER_BUTTON_DPAD_LEFT, GBA_LEFT }, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, GBA_RIGHT },
	};
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (SDL_GameControllerGetButton(pad, map[i].b))
			k |= map[i].k;
	int x = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
	int y = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
	if (x < -12000) k |= GBA_LEFT;
	if (x > 12000) k |= GBA_RIGHT;
	if (y < -12000) k |= GBA_UP;
	if (y > 12000) k |= GBA_DOWN;
	if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) k |= GBA_L;
	if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) k |= GBA_R;
	return k;
}

static uint16_t keyboardKeys(const Uint8* ks, int player) {
	uint16_t k = 0;
	if (player == 0) {
		if (ks[SDL_SCANCODE_W]) k |= GBA_UP;
		if (ks[SDL_SCANCODE_S]) k |= GBA_DOWN;
		if (ks[SDL_SCANCODE_A]) k |= GBA_LEFT;
		if (ks[SDL_SCANCODE_D]) k |= GBA_RIGHT;
		if (ks[SDL_SCANCODE_J]) k |= GBA_B;
		if (ks[SDL_SCANCODE_K]) k |= GBA_A;
		if (ks[SDL_SCANCODE_U]) k |= GBA_L;
		if (ks[SDL_SCANCODE_I]) k |= GBA_R;
		if (ks[SDL_SCANCODE_RETURN]) k |= GBA_START;
		if (ks[SDL_SCANCODE_RSHIFT]) k |= GBA_SELECT;
	} else {
		if (ks[SDL_SCANCODE_UP]) k |= GBA_UP;
		if (ks[SDL_SCANCODE_DOWN]) k |= GBA_DOWN;
		if (ks[SDL_SCANCODE_LEFT]) k |= GBA_LEFT;
		if (ks[SDL_SCANCODE_RIGHT]) k |= GBA_RIGHT;
		if (ks[SDL_SCANCODE_KP_1]) k |= GBA_B;
		if (ks[SDL_SCANCODE_KP_2]) k |= GBA_A;
		if (ks[SDL_SCANCODE_KP_4]) k |= GBA_L;
		if (ks[SDL_SCANCODE_KP_5]) k |= GBA_R;
		if (ks[SDL_SCANCODE_KP_ENTER]) k |= GBA_START;
		if (ks[SDL_SCANCODE_KP_PLUS]) k |= GBA_SELECT;
	}
	return k;
}

/* ---- Headless tests -------------------------------------------------------- */

/** Writes the screens at twice the game's size, with the overlays, side by side. */
static void writePPM(const char* path) {
	FILE* f = fopen(path, "wb");
	if (!f)
		return;
	fprintf(f, "P6 %d %d 255\n", OV_W * players, OV_H);
	for (int y = 0; y < OV_H; y++)
		for (int i = 0; i < players; i++)
			for (int x = 0; x < OV_W; x++) {
				uint32_t c = p[i].video[(y / OV) * GBA_W + x / OV];
				uint32_t o = p[i].overlay[y * OV_W + x];
				int a = o >> 24;
				unsigned char px[3];
				for (int ch = 0; ch < 3; ch++)
					px[ch] = (((c >> (8 * ch)) & 0xFF) * (255 - a) + ((o >> (8 * ch)) & 0xFF) * a) / 255;
				fwrite(px, 1, 3, f);
			}
	fclose(f);
}

static uint16_t parseKeys(const char* s) {
	uint16_t k = 0;
	for (; *s; s++) switch (*s) {
		case 'A': k |= GBA_A; break; case 'B': k |= GBA_B; break; case 'E': k |= GBA_SELECT; break;
		case 'S': k |= GBA_START; break; case '>': k |= GBA_RIGHT; break; case '<': k |= GBA_LEFT; break;
		case 'U': k |= GBA_UP; break; case 'D': k |= GBA_DOWN; break; case 'R': k |= GBA_R; break;
		case 'L': k |= GBA_L; break;
	}
	return k;
}

static int saveStates(const char* path) {
	FILE* f = fopen(path, "wb");
	if (!f)
		return 0;
	for (int i = 0; i < players; i++) {
		size_t size = p[i].core->stateSize(p[i].core);
		void* st = malloc(size);
		p[i].core->saveState(p[i].core, st);
		fwrite(st, 1, size, f);
		free(st);
	}
	fclose(f);
	return 1;
}

static int loadStates(const char* path, int from, int to) {
	FILE* f = fopen(path, "rb");
	int ok = f != NULL;
	for (int i = 0; ok && i < players; i++) {
		size_t size = p[i].core->stateSize(p[i].core);
		void* st = malloc(size);
		if (fread(st, 1, size, f) != size)
			ok = 0;
		// "loadas": player `from`'s state goes into player `to`
		if (ok && (from < 0 || i == from))
			ok = p[from < 0 ? i : to].core->loadState(p[from < 0 ? i : to].core, st);
		free(st);
	}
	if (f)
		fclose(f);
	if (!ok)
		fprintf(stderr, "could not load %s\n", path);
	return ok;
}

// Headless test script. Each line:
//   WHO KEYS FRAMES     hold KEYS (A B L R S(tart) E(select) U D < > or "-") for FRAMES; WHO is 1, 2 or b (both)
//   menu WHO            press the quick inventory key
//   walkx WHO X / walky WHO Y   walk a player to that spot in the room
//   shot FILE           save both screens (2x, with the overlays)
//   poke WHO ADDR VALUE / poke16 WHO ADDR VALUE   write memory (hex)
//   peek WHO ADDR N     print memory
//   save FILE / load FILE        both games' states
//   loadas FILE FROM TO          one player's state from a two-player state file into another player (1 or 2)
//   loadsingle FILE TO           a one-game state (the harness's) into player TO
static void runScript(const char* path) {
	FILE* f = fopen(path, "r");
	char line[256];
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char who[8] = "", keys[64] = "";
		int frames = 0, from = 0, to = 0;
		unsigned addr = 0, val = 0;
		if (sscanf(line, "poke16 %7s %x %x", who, &addr, &val) == 3) {
			for (int i = 0; i < players; i++)
				if (who[0] == 'b' || who[0] - '1' == i)
					w16(&p[i], addr, val);
			continue;
		}
		if (sscanf(line, "poke %7s %x %x", who, &addr, &val) == 3) {
			for (int i = 0; i < players; i++)
				if (who[0] == 'b' || who[0] - '1' == i)
					w8(&p[i], addr, val);
			continue;
		}
		if (sscanf(line, "peek %7s %x %d", who, &addr, &frames) == 3) {
			for (int i = 0; i < players; i++)
				if (who[0] == 'b' || who[0] - '1' == i) {
					printf("p%d %08x:", i + 1, addr);
					for (int n = 0; n < frames; n++)
						printf(" %02x", r8(&p[i], addr + n));
					printf("\n");
				}
			continue;
		}
		if (sscanf(line, "walkx %7s %d", who, &to) == 2 || sscanf(line, "walky %7s %d", who, &to) == 2) {
			// walk one player until Link stands at that room coordinate
			int i = who[0] - '1', ax = line[4] == 'x';
			for (int fr = 0; fr < 600 && i >= 0 && i < players; fr++) {
				int at = r16(&p[i], COOP + (ax ? C_ROOM_X : C_ROOM_Y));
				uint16_t kk[2] = { 0, 0 };
				int none[2] = { 0, 0 };
				if (at == to)
					break;
				kk[i] = ax ? (at < to ? GBA_RIGHT : GBA_LEFT) : (at < to ? GBA_DOWN : GBA_UP);
				hostFrame(kk, none);
			}
			continue;
		}
		if (sscanf(line, "menu %7s", who) == 1) {
			uint16_t none[2] = { 0, 0 };
			int t[2] = { who[0] == 'b' || who[0] == '1', who[0] == 'b' || who[0] == '2' };
			hostFrame(none, t);
			continue;
		}
		if (sscanf(line, "shot %63s", keys) == 1) {
			writePPM(keys);
			continue;
		}
		if (sscanf(line, "loadas %63s %d %d", keys, &from, &to) == 3) {
			loadStates(keys, from - 1, to - 1);
			continue;
		}
		if (sscanf(line, "loadsingle %63s %d", keys, &to) == 2) {
			FILE* sf = fopen(keys, "rb");
			size_t size = p[to - 1].core->stateSize(p[to - 1].core);
			void* st = malloc(size);
			if (!sf || fread(st, 1, size, sf) != size || !p[to - 1].core->loadState(p[to - 1].core, st))
				fprintf(stderr, "could not load %s\n", keys);
			if (sf)
				fclose(sf);
			free(st);
			continue;
		}
		if (sscanf(line, "save %63s", keys) == 1) {
			saveStates(keys);
			continue;
		}
		// "load WHO FILE" (WHO 1, 2 or b): a one-game state made by blend/harness, as the
		// Temple of Bonds thread's scripts use it
		if (sscanf(line, "load %7s %63s", who, keys) == 2 && who[1] == 0 &&
		    (who[0] == '1' || who[0] == '2' || who[0] == 'b')) {
			for (int i = 0; i < players; i++)
				if (who[0] == 'b' || who[0] - '1' == i) {
					struct VFile* vf = VFileOpen(keys, O_RDONLY);
					if (vf) {
						mCoreLoadStateNamed(p[i].core, vf, SAVESTATE_ALL);
						vf->close(vf);
					} else
						fprintf(stderr, "could not load %s\n", keys);
				}
			continue;
		}
		if (sscanf(line, "load %63s", keys) == 1) {
			loadStates(keys, -1, -1);
			continue;
		}
		if (sscanf(line, "%7s %63s %d", who, keys, &frames) != 3)
			continue;
		uint16_t k = parseKeys(keys);
		for (int fr = 0; fr < frames; fr++) {
			uint16_t kk[2];
			int none[2] = { 0, 0 };
			for (int i = 0; i < 2; i++)
				kk[i] = (who[0] == 'b' || who[0] - '1' == i) ? k : 0;
			hostFrame(kk, none);
		}
	}
	fclose(f);
}

/* ---- The window ------------------------------------------------------------ */

int main(int argc, char** argv) {
	const char* rom = "zelda-blend.gba";
	int vertical = 0;
	const char* testScript = NULL;
	const char* testOut = NULL;
	int checkUpdates = 1;
	struct UpdaterOptions update = { 0 };
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--single")) players = 1;
		else if (!strcmp(argv[i], "--no-update")) checkUpdates = 0;
		else if (!strcmp(argv[i], "--update-from") && i + 1 < argc) update.source = argv[++i];
		else if (!strcmp(argv[i], "--update-only")) update.onlyUpdate = update.assumeYes = 1;
		else if (!strcmp(argv[i], "--version")) {
			printf("Zelda: The Picori Seal %s\n", updaterVersion());
			return 0;
		}
		else if (!strcmp(argv[i], "--vertical")) vertical = 1;
		else if (!strcmp(argv[i], "--test") && i + 2 < argc) { testScript = argv[++i]; testOut = argv[++i]; }
		else rom = argv[i];
	}
	mLogSetDefaultLogger(&logger);
	if (update.onlyUpdate)
		return updaterRun(&update) ? 0 : 2;
	if (checkUpdates && !testScript && updaterRun(&update))
		return 0; // the new version has been started

	p[0].index = 0;
	p[1].index = 1;
	if (!startPlayer(&p[0], rom, "player1.sav") || (players == 2 && !startPlayer(&p[1], rom, "player2.sav"))) {
		char msg[512];
		snprintf(msg, sizeof(msg), "Could not load the game from \"%s\".\n\nBuild zelda-blend.gba from your own ROM "
		         "(see README) and put it next to this program.", rom);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Zelda: The Picori Seal", msg, NULL);
		return 1;
	}

	if (testScript) {
		runScript(testScript);
		writePPM(testOut);
		return 0;
	}

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}
	int outW = vertical ? GBA_W : GBA_W * players, outH = vertical ? GBA_H * players : GBA_H;
	SDL_Window* win = SDL_CreateWindow("Zelda: The Picori Seal", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	                                   outW * SCALE, outH * SCALE, SDL_WINDOW_RESIZABLE);
	SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	SDL_RenderSetLogicalSize(ren, outW * OV, outH * OV);
	SDL_Texture* tex[2];
	SDL_Texture* ovTex[2];
	for (int i = 0; i < players; i++) {
		tex[i] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_W, GBA_H);
		ovTex[i] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, OV_W, OV_H);
		SDL_SetTextureBlendMode(ovTex[i], SDL_BLENDMODE_BLEND);
	}

	SDL_AudioSpec want = { .freq = AUDIO_RATE, .format = AUDIO_S16SYS, .channels = 2, .samples = 1024 }, have;
	SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (audio)
		SDL_PauseAudioDevice(audio, 0);

	int running = 1, fullscreen = 0;
	int menuKeyWas[2] = { 0, 0 };
	Uint64 freq = SDL_GetPerformanceFrequency(), next = SDL_GetPerformanceCounter();
	const double frameTime = 280896.0 / 16777216.0; // one GBA frame
	while (running) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) running = 0;
			else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = 0;
			else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F11) {
				fullscreen = !fullscreen;
				SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
			} else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F1 && players == 2) {
				vertical = !vertical;
				outW = vertical ? GBA_W : GBA_W * 2;
				outH = vertical ? GBA_H * 2 : GBA_H;
				SDL_RenderSetLogicalSize(ren, outW * OV, outH * OV);
				SDL_SetWindowSize(win, outW * SCALE, outH * SCALE);
			} else if (e.type == SDL_CONTROLLERDEVICEADDED) {
				for (int i = 0; i < players; i++)
					if (!p[i].pad) { p[i].pad = SDL_GameControllerOpen(e.cdevice.which); break; }
			}
		}
		const Uint8* ks = SDL_GetKeyboardState(NULL);
		uint16_t keys[2] = { 0, 0 };
		int toggle[2] = { 0, 0 };
		for (int i = 0; i < players; i++) {
			int menuKey = (i == 0 ? ks[SDL_SCANCODE_Q] : ks[SDL_SCANCODE_KP_0]) ||
			              (p[i].pad && SDL_GameControllerGetButton(p[i].pad, SDL_CONTROLLER_BUTTON_Y));
			toggle[i] = menuKey && !menuKeyWas[i];
			menuKeyWas[i] = menuKey;
			keys[i] = keyboardKeys(ks, i) | padKeys(p[i].pad);
		}
		hostFrame(keys, toggle);

		// Audio: mix both games at half volume each (a game at half speed leaves gaps, filled with silence)
		if (audio) {
			int16_t mix[2][2048 * 2];
			int n = 0;
			for (int i = 0; i < players; i++) {
				int avail = blip_samples_avail(p[i].core->getAudioChannel(p[i].core, 0));
				if (avail > n)
					n = avail;
			}
			if (n > 2048) n = 2048;
			for (int i = 0; i < players; i++) {
				int avail = blip_samples_avail(p[i].core->getAudioChannel(p[i].core, 0));
				if (avail > n) avail = n;
				memset(mix[i], 0, sizeof(mix[i]));
				blip_read_samples(p[i].core->getAudioChannel(p[i].core, 0), mix[i], avail, 1);
				blip_read_samples(p[i].core->getAudioChannel(p[i].core, 1), mix[i] + 1, avail, 1);
			}
			if (players == 2)
				for (int s = 0; s < n * 2; s++)
					mix[0][s] = (int16_t) ((mix[0][s] + mix[1][s]) / 2);
			if (SDL_GetQueuedAudioSize(audio) < (Uint32) (AUDIO_RATE / 5 * 4))
				SDL_QueueAudio(audio, mix[0], n * 4);
		}

		SDL_RenderClear(ren);
		for (int i = 0; i < players; i++) {
			SDL_UpdateTexture(tex[i], NULL, p[i].video, GBA_W * sizeof(color_t));
			SDL_UpdateTexture(ovTex[i], NULL, p[i].overlay, OV_W * sizeof(uint32_t));
			SDL_Rect dst = { vertical ? 0 : i * OV_W, vertical ? i * OV_H : 0, OV_W, OV_H };
			SDL_RenderCopy(ren, tex[i], NULL, &dst);
			SDL_RenderCopy(ren, ovTex[i], NULL, &dst);
		}
		SDL_RenderPresent(ren);

		next += (Uint64) (frameTime * freq);
		Uint64 now = SDL_GetPerformanceCounter();
		if (next > now)
			SDL_Delay((Uint32) ((next - now) * 1000 / freq));
		else
			next = now;
	}

	for (int i = 0; i < players; i++)
		p[i].core->deinit(p[i].core);
	SDL_Quit();
	return 0;
}
