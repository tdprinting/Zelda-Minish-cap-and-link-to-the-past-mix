# Zelda: Minish Cap × A Link to the Past

A project to blend *The Legend of Zelda: The Minish Cap* and *The Legend of Zelda: A Link to the Past* into one game.

## Source code in this repo

| Folder | Game | Upstream | Imported commit |
| --- | --- | --- | --- |
| [`tmc/`](tmc/) | The Minish Cap (GBA decompilation) | [zeldaret/tmc](https://github.com/zeldaret/tmc) | `6fb6dfb4a7efbe24d0fd1dda5097af6131faacde` |
| [`zelda3/`](zelda3/) | A Link to the Past (C reimplementation, runs on PC) | [snesrev/zelda3](https://github.com/snesrev/zelda3) | `45a149d8b4bb8098f53d1009f76e76e7d8b79bbf` |

Both were copied in as plain files (no submodules, no upstream git history). Each folder keeps its own README and build docs; see `tmc/INSTALL.md` and `zelda3/BUILDING.md`.

**No ROMs or extracted game assets are included.** Both projects need you to supply your own legally obtained ROM:

- `tmc/` expects `baserom.gba` (USA) to build; see its INSTALL.md.
- `zelda3/` expects `zelda3.sfc` (US) to extract assets; see its BUILDING.md.

## Licensing

- `zelda3/` is under its own license in `zelda3/LICENSE.txt`.
- `tmc/` has no license file upstream.
