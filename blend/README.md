# Zelda: The Picori Seal (Minish Cap × A Link to the Past)

The blended game is built on the Minish Cap decompilation in [`../tmc`](../tmc), with
A Link to the Past systems and content brought over from [`../zelda3`](../zelda3).
Everything is made by restyling and reusing the art already in Minish Cap.

## What's in it so far

| Feature | From | How it works |
| --- | --- | --- |
| Dark World | A Link to the Past | Every overworld area has a Dark World version. Its palettes are restyled at runtime (olive grass, brown earth, violet shadows), so no new art is needed. |
| Magic Mirror | A Link to the Past | Comes with the Fire Element from Mt. Crenel (this game's Death Mountain). Then hold **L + R** on the overworld to cross between the Light and Dark World. The room reloads in the other world, and the current world is saved with your file. |
| Moon Pearl | A Link to the Past | Without it Link is a pink bunny in the Dark World and can't use his sword or items. It comes with the Ocarina after the Fortress of Winds, the third dungeon, like the Tower of Hera. |
| Fire Rod and Ice Rod | A Link to the Past | Minish Cap's unused Fire Rod, finished, lies in a clearing of the Dark World Minish Woods (Skull Woods). The Ice Rod waits on Lake Hylia's stump island for anyone with Flippers. Link swings them like the Pacci Cane: one shoots a red fireball, the other a white ice bolt. They share the cane's menu slot, and **L + A** or **L + B** cycles the rod on that button. |
| Bombos, Ether, Quake | A Link to the Past | Medallions found in the Dark World: Bombos buried in the ruins (dig with the Mole Mitts), Ether on the Mt. Crenel summit, Quake on the Lake Hylia stump island. **L + Select** picks one, **R + Select** casts it. A cast needs a sword, works once per room and strikes every enemy on screen. |
| Hookshot, Hammer, Canes of Byrna and Somaria, Magic Powder | A Link to the Past | Each one shares a menu slot with a Minish Cap item, since the menu has no room left: Hookshot with the Gust Jar, Hammer with the Mole Mitts, Byrna with the Shield, Powder with the Lantern, Somaria with the Bombs. Once you own both, **L + the item's button** switches that slot. The Hookshot grabs walls and pulls Link across, the Hammer smashes and stuns, Byrna wraps Link in a damaging, protective ring, Powder turns weak enemies into fairies, and Somaria makes a pushable block (one per room). Where they are: Hookshot in the Dark World Castor Wilds (Swamp Palace), Hammer in the dark Eastern Hills (Palace of Darkness), Byrna buried on the dark Mt. Crenel, Powder in the Light World Minish Woods once you have the Lantern, Somaria at the dark Veil Falls (Misery Mire). |
| Eastern Palace | A Link to the Past | The first A Link to the Past dungeon, room for room: its walls, pits, doors, stairs, chests and enemies come from the original layout and are drawn with Minish Cap's Fortress of Winds tiles. The cracked cave in the Eastern Hills opens once Link has the Earth Element. Inside are the map, compass, a small key, the Big Key and the Bow in the big chest, key doors, a basement, and the Armos Knights; beating them brings out a Heart Container. |
| Dark World enemies | A Link to the Past | In the Dark World, Octoroks become Ropes, ChuChus become Spiny ChuChus and Beetles become Spiked Beetles. |
| Blended story | Both | The opening legend tells of the Golden Power, the shadow sealed in a Dark World and the seven sages. Ezlo explains Ganon, Vaati and Agahnim the first time Link reaches the Dark World, and the castle and final boss text name Ganon's shadow. |
| Split-screen co-op | New | Two copies of the game run side by side in one window. Each player sees the other as a second Link in red mail when they are in the same room and world, including sword swings. The partner's sword hits enemies on your screen too. |

## Playing on Windows

1. Build `zelda-blend.gba` from your own Minish Cap (USA) ROM (below).
2. Put `zelda-blend.exe` and `zelda-blend.gba` in the same folder and run the exe.

| Player 1 (keyboard) | Player 2 (keyboard) | GBA button |
| --- | --- | --- |
| W A S D | Arrow keys | D-pad |
| K | Numpad 2 | A |
| J | Numpad 1 | B |
| U / I | Numpad 4 / 5 | L / R |
| Enter | Numpad Enter | Start |
| Right Shift | Numpad + | Select |

Gamepads work too: the first controller is player 1, the second is player 2.
**F1** switches between side-by-side and stacked screens, **F11** toggles fullscreen,
**Esc** quits. Each player has their own save (`player1.sav`, `player2.sav`).
Run `zelda-blend.exe --single` to play alone.

The ROM also runs in any GBA emulator for single-player.

## Building

ROMs and anything built from them stay on your machine; `.gitignore` blocks them.

```sh
# The game ROM (Linux or WSL; toolchain in tmc/INSTALL.md)
./blend/build.sh path/to/minish-cap-usa.gba path/to/alttp-usa.sfc   # -> blend/out/zelda-blend.gba

# The co-op player, Linux
make -C blend/host                               # needs libsdl2-dev, libmgba-dev

# The co-op player, Windows (cross-compiled; see the script for its dependencies)
./blend/host/build-windows.sh                    # -> blend/out/zelda-blend.exe
```

## Code map

| File | What it does |
| --- | --- |
| `tmc/src/blend.c`, `tmc/include/blend.h` | Dark World palettes, Magic Mirror, Moon Pearl and bunny, rods, medallions, shared-slot items, item placement, co-op state sync, partner Link object |
| `tmc/src/objectUtils.c`, `tmc/src/scroll.c`, `tmc/src/ui.c` | Borrowed pictures for A Link to the Past items on the ground, held up and on the HUD |
| `tmc/src/fade.c` | Applies the Dark World palette as palettes are uploaded |
| `tmc/src/game.c` | Calls the blend update every gameplay frame |
| `blend/dungeons/` | Rebuilds A Link to the Past dungeons: `make-dungeons.sh` runs zelda3's room loader over your ROM, `build.py` turns each room into a Minish Cap room (`autotile.py` learns the tileset from Minish Cap's own rooms) and writes `tmc/custom/gen/` |
| `blend/host/coop.c` | Split-screen co-op player (SDL2 + libmgba) |
| `blend/harness/harness.c` | Headless test runner used to check builds |

The shared state block `gBlendLink` sits at a fixed address, `0x0203F000`, so the
host can read each player's position and write the other player's in.
