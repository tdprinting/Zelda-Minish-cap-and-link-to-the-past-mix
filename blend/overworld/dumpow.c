// Headless dump of A Link to the Past's overworld: for each overworld area (Light World
// 0x00-0x3f, Dark World 0x40-0x7f) loads its graphics, colors and map with zelda3's own
// code and writes them out, so blend/overworld/exteriors.py can cut the dungeon
// fronts and landmarks out of them.
//   dumpow OUTDIR            -> OUTDIR/ow_XX.bin per area, OUTDIR/tables.bin once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/types.h"
#include "src/variables.h"
#include "src/assets.h"
#include "src/overworld.h"
#include "src/misc.h"
#include "src/util.h"
#include "src/config.h"
#include "src/zelda_rtl.h"
#include "src/load_gfx.h"

const uint8 *g_asset_ptrs[kNumberOfAssets];
uint32 g_asset_sizes[kNumberOfAssets];
MemBlk FindInAssetArray(int asset, int idx) {
  return FindIndexInMemblk((MemBlk) { g_asset_ptrs[asset], g_asset_sizes[asset] }, idx);
}
void Die(const char *e) { fprintf(stderr, "die: %s\n", e); exit(1); }

static void LoadAssets(void) {
  size_t length = 0;
  uint8 *data = ReadWholeFile("zelda3_assets.dat", &length);
  if (!data) Die("no assets");
  uint32 offset = 88 + kNumberOfAssets * 4 + *(uint32 *)(data + 84);
  for (size_t i = 0; i < kNumberOfAssets; i++) {
    uint32 size = *(uint32 *)(data + 88 + i * 4);
    offset = (offset + 3) & ~3;
    g_asset_sizes[i] = size;
    g_asset_ptrs[i] = data + offset;
    offset += size;
  }
}

int main(int argc, char **argv) {
  const char *outdir = argc > 1 ? argv[1] : "ow";
  char name[256];
  LoadAssets();
  ZeldaInitialize();
  snprintf(name, sizeof(name), "%s/tables.bin", outdir);
  FILE *f = fopen(name, "wb");
  uint32 n = kMap16ToMap8_SIZE;
  fwrite(&n, 4, 1, f);
  fwrite(kMap16ToMap8, 1, n, f);                 // map16 -> 4 map8 words (TL, TR, BL, BR)
  n = kMap8DataToTileAttr_SIZE;
  fwrite(&n, 4, 1, f);
  fwrite(kMap8DataToTileAttr, 1, n, f);          // map8 character -> tile attribute
  fclose(f);
  // screens 0x00-0x7f, then the special areas 0x80 (the Master Sword's grove) and 0x81 (Zora's waterfall)
  for (int sc = 0; sc < 0x82; sc++) {
    memset(g_ram, 0, 0x20000);
    memset(g_zenv.vram, 0, 0x10000);
    main_module_index = 8;
    savegame_is_darkworld = sc & 0x40;
    sram_progress_indicator = 3;
    overworld_screen_index = overworld_area_index = sc;
    Overworld_LoadGFXAndScreenSize();
    int t = sc & ~0x40, special = sc >= 0x80;
    if (special) {
      savegame_is_darkworld = 0;
      sprite_graphics_index = kSpExit_SprGfx[sc - 0x80];
      aux_tile_theme_index = kSpExit_AuxGfx[sc - 0x80];
    }
    DecompressAnimatedOverworldTiles((t == 3 || t == 5 || t == 7) ? 0x58 : 0x5a);
    InitializeTilesets();
    OverworldLoadScreensPaletteSet();
    if (special)
      Overworld_LoadPalettes(kSpExit_PalBg[sc - 0x80], kSpExit_PalSpr[sc - 0x80]);
    else
      Overworld_LoadPalettes(GetOverworldBgPalette(sc), 0);
    Overworld_CopyPalettesToCache();
    Overworld_DecompressAndDrawAllQuadrants();
    snprintf(name, sizeof(name), "%s/ow_%02x.bin", outdir, sc);
    f = fopen(name, "wb");
    uint8 big = special || kOverworldMapIsSmall[sc & 0x3f] ? 0 : 1;
    fwrite(&big, 1, 1, f);
    fwrite(&g_ram[0x2000], 2, 64 * 64, f);       // map16 numbers, 64x64 (a small area uses the top-left 32x32)
    fwrite(g_zenv.vram, 2, 0x8000, f);           // video memory (background characters at word 0x2000)
    fwrite(main_palette_buffer, 2, 256, f);      // colors, BGR555
    fclose(f);
  }
  return 0;
}
void ZeldaApuLock(void) {}
void ZeldaApuUnlock(void) {}
