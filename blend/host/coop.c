// Split-screen co-op host for the Minish Cap x A Link to the Past blend.
//
// Runs two copies of the blended game side by side in one window, one per
// player. Every frame it copies each player's position and animation into the
// other game's shared state block (gBlendLink at 0x0203F000), where the game
// draws them as a second Link in red mail.
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
//   Player 1: WASD move, J = B, K = A, U = L, I = R, Enter = Start, Right Shift = Select
//   Player 2: arrows move, Numpad 1 = B, Numpad 2 = A, Numpad 4 = L, Numpad 5 = R,
//             Numpad Enter = Start, Numpad + = Select
//   F1 toggles side-by-side and stacked layout, F11 toggles fullscreen, Esc quits.
#include <mgba/core/core.h>
#include <mgba/core/blip_buf.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba-util/vfs.h>

#include <SDL.h>
#include "updater.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GBA_W 240
#define GBA_H 160
#define SCALE 3
#define AUDIO_RATE 44100

// Layout of gBlendLink (see tmc/include/blend.h)
#define LINK_BASE 0x0203F000u
#define LINK_HOST_CONNECTED (LINK_BASE + 0x04)
#define LINK_PLAYER_INDEX (LINK_BASE + 0x05)
#define LINK_SELF (LINK_BASE + 0x10)
#define LINK_PARTNER (LINK_BASE + 0x34)
#define LINK_SYNC_SIZE 0x24

enum {
	GBA_A = 1 << 0, GBA_B = 1 << 1, GBA_SELECT = 1 << 2, GBA_START = 1 << 3,
	GBA_RIGHT = 1 << 4, GBA_LEFT = 1 << 5, GBA_UP = 1 << 6, GBA_DOWN = 1 << 7,
	GBA_R = 1 << 8, GBA_L = 1 << 9,
};

struct Player {
	struct mCore* core;
	color_t* video;
	SDL_GameController* pad;
};

static void quietLog(struct mLogger* logger, int category, enum mLogLevel level, const char* format, va_list args) {
	(void) logger; (void) category; (void) args;
	if (level & (mLOG_FATAL | mLOG_ERROR)) {
		vfprintf(stderr, format, args);
		fputc('\n', stderr);
	}
}
static struct mLogger logger = { .log = quietLog };

static int startPlayer(struct Player* p, const char* rom, const char* save) {
	p->core = mCoreFind(rom);
	if (!p->core || !p->core->init(p->core)) {
		fprintf(stderr, "Could not start the game core for %s\n", rom);
		return 0;
	}
	mCoreInitConfig(p->core, NULL);
	p->video = calloc(GBA_W * GBA_H, sizeof(color_t));
	p->core->setVideoBuffer(p->core, p->video, GBA_W);
	p->core->setAudioBufferSize(p->core, 2048);
	blip_set_rates(p->core->getAudioChannel(p->core, 0), p->core->frequency(p->core), AUDIO_RATE);
	blip_set_rates(p->core->getAudioChannel(p->core, 1), p->core->frequency(p->core), AUDIO_RATE);
	if (!mCoreLoadFile(p->core, rom)) {
		fprintf(stderr, "Could not load %s\n", rom);
		return 0;
	}
	mCoreLoadSaveFile(p->core, save, false);
	p->core->reset(p->core);
	return 1;
}

static void exchangeState(struct Player* a, struct Player* b) {
	uint8_t sa[LINK_SYNC_SIZE], sb[LINK_SYNC_SIZE];
	for (int i = 0; i < LINK_SYNC_SIZE; i++) {
		sa[i] = a->core->busRead8(a->core, LINK_SELF + i);
		sb[i] = b->core->busRead8(b->core, LINK_SELF + i);
	}
	for (int i = 0; i < LINK_SYNC_SIZE; i++) {
		a->core->busWrite8(a->core, LINK_PARTNER + i, sb[i]);
		b->core->busWrite8(b->core, LINK_PARTNER + i, sa[i]);
	}
	a->core->busWrite8(a->core, LINK_HOST_CONNECTED, 1);
	b->core->busWrite8(b->core, LINK_HOST_CONNECTED, 1);
	a->core->busWrite8(a->core, LINK_PLAYER_INDEX, 0);
	b->core->busWrite8(b->core, LINK_PLAYER_INDEX, 1);
}

static uint16_t padKeys(SDL_GameController* pad) {
	uint16_t k = 0;
	if (!pad)
		return 0;
	static const struct { SDL_GameControllerButton b; uint16_t k; } map[] = {
		{ SDL_CONTROLLER_BUTTON_A, GBA_B }, { SDL_CONTROLLER_BUTTON_B, GBA_A },
		{ SDL_CONTROLLER_BUTTON_X, GBA_B }, { SDL_CONTROLLER_BUTTON_Y, GBA_A },
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

static void writePPM(const char* path, struct Player* players, int count) {
	FILE* f = fopen(path, "wb");
	if (!f)
		return;
	fprintf(f, "P6 %d %d 255\n", GBA_W * count, GBA_H);
	for (int y = 0; y < GBA_H; y++)
		for (int p = 0; p < count; p++)
			for (int x = 0; x < GBA_W; x++) {
				uint32_t c = players[p].video[y * GBA_W + x];
				unsigned char px[3] = { c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF };
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

// Headless test script. Each line: "WHO KEYS FRAMES" where WHO is 1, 2 or b (both)
// and KEYS uses A B L R S(tart) E(select) U D < > or "-" for none; "shot FILE" saves the screens;
// "poke WHO ADDR VALUE" writes a byte (hex) into that player's memory, "poke16" a halfword;
// "load WHO FILE" loads a save state (made by blend/harness) into that player's game;
// "peek WHO ADDR COUNT" prints COUNT bytes (hex) of that player's memory.
static void runScript(const char* path, struct Player* p, int players) {
	FILE* f = fopen(path, "r");
	char line[256];
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char who[8] = "", keys[32] = "";
		int frames = 0;
		unsigned addr = 0, val = 0;
		if (strncmp(line, "poke16", 6) && sscanf(line, "poke %7s %x %x", who, &addr, &val) == 3) {
			for (int i = 0; i < players; i++)
				if (who[0] == 'b' || who[0] - '1' == i)
					p[i].core->busWrite8(p[i].core, addr, val);
			continue;
		}
		if (sscanf(line, "poke16 %7s %x %x", who, &addr, &val) == 3) {
			for (int i = 0; i < players; i++)
				if (who[0] == 'b' || who[0] - '1' == i)
					p[i].core->busWrite16(p[i].core, addr, val);
			continue;
		}
		if (sscanf(line, "peek %7s %x %u", who, &addr, &val) == 3) {
			for (int i = 0; i < players; i++)
				if (who[0] == 'b' || who[0] - '1' == i) {
					printf("peek %d %08x:", i + 1, addr);
					for (unsigned n = 0; n < val; n++)
						printf(" %02x", p[i].core->busRead8(p[i].core, addr + n));
					printf("\n");
				}
			continue;
		}
		char file[200];
		if (sscanf(line, "load %7s %199s", who, file) == 2) {
			for (int i = 0; i < players; i++)
				if (who[0] == 'b' || who[0] - '1' == i) {
					struct VFile* vf = VFileOpen(file, O_RDONLY);
					if (vf) {
						mCoreLoadStateNamed(p[i].core, vf, SAVESTATE_ALL);
						vf->close(vf);
					}
				}
			continue;
		}
		if (sscanf(line, "shot %31s", keys) == 1) {
			writePPM(keys, p, players);
			continue;
		}
		if (sscanf(line, "%7s %31s %d", who, keys, &frames) != 3)
			continue;
		uint16_t k = parseKeys(keys);
		for (int i = 0; i < players; i++)
			p[i].core->setKeys(p[i].core, (who[0] == 'b' || who[0] - '1' == i) ? k : 0);
		for (int fr = 0; fr <= frames; fr++) {
			if (players == 2)
				exchangeState(&p[0], &p[1]);
			for (int i = 0; i < players; i++)
				p[i].core->runFrame(p[i].core);
			if (fr == frames - 1)
				for (int i = 0; i < players; i++)
					p[i].core->setKeys(p[i].core, 0);
		}
	}
	fclose(f);
}

int main(int argc, char** argv) {
	const char* rom = "zelda-blend.gba";
	int players = 2, vertical = 0;
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

	struct Player p[2] = { 0 };
	if (!startPlayer(&p[0], rom, "player1.sav") || (players == 2 && !startPlayer(&p[1], rom, "player2.sav"))) {
		char msg[512];
		snprintf(msg, sizeof(msg), "Could not load the game from \"%s\".\n\nBuild zelda-blend.gba from your own ROM "
		         "(see README) and put it next to this program.", rom);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Zelda: The Picori Seal", msg, NULL);
		return 1;
	}

	if (testScript) {
		runScript(testScript, p, players);
		writePPM(testOut, p, players);
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
	SDL_RenderSetLogicalSize(ren, outW, outH);
	SDL_Texture* tex[2];
	for (int i = 0; i < players; i++)
		tex[i] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_W, GBA_H);

	SDL_AudioSpec want = { .freq = AUDIO_RATE, .format = AUDIO_S16SYS, .channels = 2, .samples = 1024 }, have;
	SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (audio)
		SDL_PauseAudioDevice(audio, 0);

	int running = 1, fullscreen = 0;
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
				SDL_RenderSetLogicalSize(ren, outW, outH);
				SDL_SetWindowSize(win, outW * SCALE, outH * SCALE);
			} else if (e.type == SDL_CONTROLLERDEVICEADDED) {
				for (int i = 0; i < players; i++)
					if (!p[i].pad) { p[i].pad = SDL_GameControllerOpen(e.cdevice.which); break; }
			}
		}
		const Uint8* ks = SDL_GetKeyboardState(NULL);
		for (int i = 0; i < players; i++)
			p[i].core->setKeys(p[i].core, keyboardKeys(ks, i) | padKeys(p[i].pad));
		if (players == 2)
			exchangeState(&p[0], &p[1]);
		for (int i = 0; i < players; i++)
			p[i].core->runFrame(p[i].core);

		// Audio: mix both games at half volume each
		if (audio) {
			int16_t mix[2][2048 * 2];
			int n = blip_samples_avail(p[0].core->getAudioChannel(p[0].core, 0));
			if (n > 2048) n = 2048;
			for (int i = 0; i < players; i++) {
				blip_read_samples(p[i].core->getAudioChannel(p[i].core, 0), mix[i], n, 1);
				blip_read_samples(p[i].core->getAudioChannel(p[i].core, 1), mix[i] + 1, n, 1);
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
			SDL_Rect dst = { vertical ? 0 : i * GBA_W, vertical ? i * GBA_H : 0, GBA_W, GBA_H };
			SDL_RenderCopy(ren, tex[i], NULL, &dst);
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
