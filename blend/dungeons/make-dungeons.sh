#!/bin/sh
# Rebuilds A Link to the Past's dungeons for the blend from your own ALttP (USA) ROM.
#   ./blend/dungeons/make-dungeons.sh path/to/alttp-usa.sfc
# Works in blend/out/z3 and writes tmc/custom/gen/. Both stay out of git: they hold data
# from your ROM. Needs python3 (pyyaml, pillow), gcc and SDL2 development files.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ROM=${1:?usage: make-dungeons.sh path/to/alttp-usa.sfc}
ROM=$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")
WORK=$ROOT/blend/out/z3
mkdir -p "$WORK"
cp -r "$ROOT/zelda3/." "$WORK/"
cd "$WORK"
# zelda3's own extractor: room headers and sprites as YAML, plus zelda3_assets.dat
python3 assets/restool.py --extract-from-rom --rom "$ROM"
# zelda3's room loader, run headlessly over every dungeon room
mkdir -p obj rooms
for f in snes/*.c src/*.c third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c; do
    case "$f" in src/main.c|src/opengl.c|src/glsl_shader.c) continue ;; esac
    o=obj/$(echo "$f" | tr / _).o
    [ "$o" -nt "$f" ] || gcc -c -O1 -w -I. $(sdl2-config --cflags) "$f" -o "$o"
done
gcc -c -O1 -w -I. $(sdl2-config --cflags) "$ROOT/blend/dungeons/dumprooms.c" -o obj/dumprooms.o
gcc obj/*.o -o dumprooms $(sdl2-config --libs) -lm
./dumprooms rooms
ALTTP_ROOMS="$WORK/rooms" ALTTP_YAML="$WORK/assets/dungeon" python3 "$ROOT/blend/dungeons/build.py"
