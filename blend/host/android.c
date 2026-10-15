// Android side of the co-op host (see platform.h). Built into the APK by
// blend/android/build-apk.sh.
//
// - The game is zelda-blend.gba from inside the APK, unless a newer copy has been
//   put in the app's files folder (by the updater, or by hand over USB):
//   /sdcard/Android/data/com.picoriseal.game/files/zelda-blend.gba. A copy older
//   than the installed APK is ignored, so installing a new APK always wins.
// - Saves (player1.sav, player2.sav) live in that same folder.
// - PicoriActivity.java passes in the APK's install time (PICORI_APK_TIME) and the
//   names of the device's built-in controls (PICORI_BUILTIN_PADS, read by coop.c).
#include "platform.h"

#include <mgba-util/vfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define GAME_FILE "zelda-blend.gba"

void platformInit(void) {
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
	SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1"); // Back must not quit mid-game
	SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
}

static const char* dataDir(void) {
	static char dir[512];
	if (!dir[0]) {
		const char* d = getenv("PICORI_DATA_DIR"); // tests run without Java
		if (!d)
			d = SDL_AndroidGetExternalStoragePath();
		if (!d)
			d = SDL_AndroidGetInternalStoragePath();
		snprintf(dir, sizeof(dir), "%s", d ? d : ".");
	}
	return dir;
}

const char* platformDataPath(const char* name) {
	static char paths[4][640];
	static int next;
	char* path = paths[next++ % 4];
	snprintf(path, sizeof(paths[0]), "%s/%s", dataDir(), name);
	return path;
}

struct VFile* platformOpenGame(const char* rom) {
	if (rom)
		return VFileOpen(rom, O_RDONLY);

	const char* updated = platformDataPath(GAME_FILE);
	const char* apkTime = getenv("PICORI_APK_TIME");
	struct stat st;
	if (!stat(updated, &st) && st.st_size > 0 && st.st_mtime >= (apkTime ? atoll(apkTime) : 0)) {
		SDL_Log("Using the updated game at %s", updated);
		return VFileOpen(updated, O_RDONLY);
	}

	// Both players share one read-only copy of the game from the APK
	static void* game;
	static size_t size;
	if (!game) {
		SDL_RWops* rw = SDL_RWFromFile(GAME_FILE, "rb");
		Sint64 len = rw ? SDL_RWsize(rw) : -1;
		if (len > 0 && (game = malloc(len)) && SDL_RWread(rw, game, 1, len) == (size_t) len)
			size = len;
		else {
			free(game);
			game = NULL;
		}
		if (rw)
			SDL_RWclose(rw);
		if (!game)
			return NULL;
	}
	return VFileFromConstMemory(game, size);
}
