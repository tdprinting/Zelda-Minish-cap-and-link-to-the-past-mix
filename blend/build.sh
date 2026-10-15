#!/bin/sh
# Builds the blended game from your own Minish Cap (USA) and A Link to the Past (USA) ROMs.
#   ./blend/build.sh path/to/minish-cap-usa.gba path/to/alttp-usa.sfc
# The ALttP ROM supplies the dungeon layouts and Link's item art; it can be left out once tmc/custom/gen exists.
# Output: blend/out/zelda-blend.gba (private: it contains data from your ROM).
# Needs the tmc toolchain (agbcc, arm-none-eabi binutils, cmake, libpng); see tmc/INSTALL.md.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ROM=${1:-$ROOT/tmc/baserom.gba}
EXPECTED=b4bd50e4131b027c334547b4524e2dbbd4227130
SUM=$(sha1sum "$ROM" | cut -d' ' -f1)
if [ "$SUM" != "$EXPECTED" ]; then
    echo "That ROM is not The Minish Cap (USA). Expected SHA1 $EXPECTED, got $SUM." >&2
    exit 1
fi
[ "$ROM" -ef "$ROOT/tmc/baserom.gba" ] || cp "$ROM" "$ROOT/tmc/baserom.gba"
cd "$ROOT/tmc"
make tools
make -j"$(nproc)" CUSTOM=1 build_assets
if [ -n "$2" ]; then
    "$ROOT/blend/dungeons/make-dungeons.sh" "$2"
elif [ ! -f custom/gen/maps.inc ] || [ ! -f custom/gen/blend_link.inc ]; then
    echo "Pass your A Link to the Past (USA) ROM as the second argument to build its dungeons." >&2
    exit 1
fi
# splice every generated frame object list into the game's table
python3 "$ROOT/blend/anim/objlists.py"
make -j"$(nproc)" CUSTOM=1
mkdir -p "$ROOT/blend/out"
cp tmc.gba "$ROOT/blend/out/zelda-blend.gba"
echo "Built blend/out/zelda-blend.gba"
