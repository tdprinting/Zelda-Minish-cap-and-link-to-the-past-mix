#!/bin/sh
# Builds the blended game from your own Minish Cap (USA) ROM.
#   ./blend/build.sh path/to/minish-cap-usa.gba
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
make -j"$(nproc)" CUSTOM=1
mkdir -p "$ROOT/blend/out"
cp tmc.gba "$ROOT/blend/out/zelda-blend.gba"
echo "Built blend/out/zelda-blend.gba"
