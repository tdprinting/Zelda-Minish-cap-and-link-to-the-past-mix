#!/bin/sh
# Cross-compiles the split-screen co-op player for Windows from Linux/WSL.
# Needs mingw-w64 and static builds of SDL2 (2.30.x) and libmgba (0.10.x) for
# mingw installed under $WIN (default ./deps/win), with the mGBA source in $MGBA.
#   cmake mGBA with: -DLIBMGBA_ONLY=ON -DBUILD_STATIC=ON -DBUILD_SHARED=OFF -DM_CORE_GB=OFF
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
WIN=${WIN:-$HERE/deps/win}
MGBA=${MGBA:-$HERE/deps/mgba}
mkdir -p "$HERE/../out"
x86_64-w64-mingw32-gcc -O2 -Wall -o "$HERE/../out/zelda-blend.exe" "$HERE/coop.c" \
    -I"$MGBA/include" -I"$MGBA/build-win/include" -I"$WIN/include/SDL2" -Dmain=SDL_main -DMGBA_STANDALONE \
    "$MGBA/build-win/libmgba.a" -L"$WIN/lib" -lmingw32 -lSDL2main -lSDL2 -mwindows -lm -lkernel32 -luser32 \
    -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -ladvapi32 -lsetupapi -lshell32 -ldinput8 \
    -lws2_32 -lshlwapi -static
echo "Built blend/out/zelda-blend.exe"
