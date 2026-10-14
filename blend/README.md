# Zelda: The Picori Seal (Minish Cap × A Link to the Past)

The blended game is built on the Minish Cap decompilation in [`../tmc`](../tmc), with
A Link to the Past systems and content brought over from [`../zelda3`](../zelda3).
Everything is made by restyling and reusing the art already in Minish Cap.

## What's in it so far

| Feature | From | How it works |
| --- | --- | --- |
| Dark World | A Link to the Past | Every overworld area has a Dark World version. Its palettes are restyled at runtime (olive grass, brown earth, violet shadows), so no new art is needed. |
| Magic Mirror | A Link to the Past | Hold **L + R** on the overworld to cross between the Light and Dark World. The current world is saved with your file. |
| Split-screen co-op | New | Two copies of the game run side by side in one window. Each player sees the other as a second Link in red mail when they are in the same room and world. |

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
./blend/build.sh path/to/minish-cap-usa.gba     # -> blend/out/zelda-blend.gba

# The co-op player, Linux
make -C blend/host                               # needs libsdl2-dev, libmgba-dev

# The co-op player, Windows (cross-compiled; see the script for its dependencies)
./blend/host/build-windows.sh                    # -> blend/out/zelda-blend.exe
```

## Code map

| File | What it does |
| --- | --- |
| `tmc/src/blend.c`, `tmc/include/blend.h` | Dark World palettes, Magic Mirror, co-op state sync, partner Link object |
| `tmc/src/fade.c` | Applies the Dark World palette as palettes are uploaded |
| `tmc/src/game.c` | Calls the blend update every gameplay frame |
| `blend/host/coop.c` | Split-screen co-op player (SDL2 + libmgba) |
| `blend/harness/harness.c` | Headless test runner used to check builds |

The shared state block `gBlendLink` sits at a fixed address, `0x0203F000`, so the
host can read each player's position and write the other player's in.
