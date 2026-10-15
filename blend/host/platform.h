// What the co-op host does differently per platform. coop.c has the desktop
// versions; android.c has the Android ones.
#pragma once
#include <SDL.h>

struct VFile;

// Sets platform hints before SDL starts.
void platformInit(void);
// Opens the game. rom is the path given on the command line, or NULL for the
// default (zelda-blend.gba next to the program, or the copy inside the APK).
struct VFile* platformOpenGame(const char* rom);
// Where a save file (or other per-user file) called name lives.
const char* platformDataPath(const char* name);
