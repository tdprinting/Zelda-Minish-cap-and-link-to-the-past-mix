// Headless dump of A Link to the Past dungeon rooms: for each room id, loads the
// room with zelda3's own code and writes its tile attribute map and tilemaps.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/types.h"
#include "src/variables.h"
#include "src/assets.h"
#include "src/dungeon.h"
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

// gfx mode: the room's background graphics and colors as A Link to the Past shows them.
// The main tileset comes from the dungeon's entrance, the rest from each room's header.
static int DumpGfx(const char *outdir, int entrance_room, char **rooms, int n) {
  int theme = -1;
  for (int i = 0; i < (int)(kEntranceData_rooms_SIZE / 2); i++) {
    if (kEntranceData_rooms[i] == entrance_room) { theme = kEntranceData_blockset[i]; break; }
  }
  if (theme < 0) Die("no entrance leads to that room");
  for (int k = 0; k < n; k++) {
    int r = (int)strtol(rooms[k], NULL, 16);
    memset(g_ram, 0, 0x20000);
    memset(g_zenv.vram, 0, 0x10000);
    Init_LoadDefaultTileAttr();
    memcpy(movable_block_datas, kMovableBlockDataInit, kMovableBlockDataInit_SIZE);
    memcpy(dung_torch_data, kTorchDataInit, kTorchDataInit_SIZE);
    main_module_index = 7;
    dungeon_room_index = r;
    Dungeon_LoadRoom();
    main_tile_theme_index = theme;
    InitializeTilesets();
    Palette_Load_DungeonSet();
    char name[256];
    snprintf(name, sizeof(name), "%s/%03x.gfx", outdir, r);
    FILE *f = fopen(name, "wb");
    fwrite(&g_zenv.vram[0x2000], 2, 0x2000, f);     // background tiles, SNES 4bpp, tile 0 first
    fwrite(aux_palette_buffer, 2, 256, f);          // colors, BGR555
    fclose(f);
  }
  return 0;
}

int main(int argc, char **argv) {
  LoadAssets();
  ZeldaInitialize();
  if (argc > 3 && !strcmp(argv[1], "gfx"))
    return DumpGfx(argv[2], (int)strtol(argv[3], NULL, 16), argv + 4, argc - 4);
  const char *outdir = argc > 1 ? argv[1] : "rooms";
  for (int r = 0; r < 296; r++) {
    memset(g_ram, 0, 0x20000);
    Init_LoadDefaultTileAttr();
    memcpy(movable_block_datas, kMovableBlockDataInit, kMovableBlockDataInit_SIZE);
    memcpy(dung_torch_data, kTorchDataInit, kTorchDataInit_SIZE);
    main_module_index = 7;
    submodule_index = 0;
    dungeon_room_index = r;
    Dungeon_LoadRoom();
    Dungeon_LoadCustomTileAttr();
    Dungeon_LoadAttributeTable();
    char name[256];
    snprintf(name, sizeof(name), "%s/%03x.bin", outdir, r);
    FILE *f = fopen(name, "wb");
    fwrite(dung_bg2_attr_table, 1, 0x2000, f);       // attributes: upper 64x64, then lower 64x64
    fwrite(dung_bg2, 2, 64 * 64, f);                 // BG2 tile words
    fwrite(dung_bg1, 2, 64 * 64, f);                 // BG1 tile words
    fwrite(dung_hdr_travel_destinations, 1, 5, f);   // hole, stair0..3
    fwrite(dung_chest_locations, 2, 6, f);
    fwrite(door_type_and_slot, 2, 16, f);          // per door: type << 8 | slot
    fwrite(dung_door_tilemap_address, 2, 16, f);   // per door: tilemap word offset of its top-left
    fwrite(dung_door_direction, 2, 16, f);
    fwrite(&dung_cur_door_idx, 2, 1, f);
    fclose(f);
  }
  return 0;
}
void ZeldaApuLock(void) {}
void ZeldaApuUnlock(void) {}
