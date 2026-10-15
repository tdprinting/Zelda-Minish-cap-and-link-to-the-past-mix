#!/bin/sh
# Packs a private release: blend/out/picori-seal-vX.Y.zip plus latest.json, the
# manifest the game's auto updater reads (version, file, sha256).
#   ./blend/host/package.sh [VERSION] [OUTDIR]
# Run blend/build.sh and blend/host/build-windows.sh first. VERSION defaults to
# blend/VERSION; bump that file before building so the exe knows its own version.
# The zip holds a game built from your ROM: keep it private (never a public host).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BLEND=$(cd "$HERE/.." && pwd)
VERSION=${1:-$(cat "$BLEND/VERSION")}
OUT=${2:-$BLEND/out}
NAME=picori-seal-v$VERSION
for f in zelda-blend.gba zelda-blend.exe; do
    [ -f "$BLEND/out/$f" ] || { echo "Missing blend/out/$f; build it first." >&2; exit 1; }
done
TMP=$(mktemp -d)
mkdir "$TMP/$NAME"
cp "$BLEND/out/zelda-blend.gba" "$BLEND/out/zelda-blend.exe" "$BLEND/README.md" "$TMP/$NAME/"
mkdir -p "$OUT"
rm -f "$OUT/$NAME.zip"
(cd "$TMP" && python3 -c "import shutil,sys; shutil.make_archive(sys.argv[1], 'zip', '.', sys.argv[2])" "$OUT/$NAME" "$NAME")
rm -rf "$TMP"
SHA=$(sha256sum "$OUT/$NAME.zip" | cut -d' ' -f1)
printf '{\n  "version": "%s",\n  "file": "%s.zip",\n  "sha256": "%s"\n}\n' "$VERSION" "$NAME" "$SHA" > "$OUT/latest.json"
echo "Packed $OUT/$NAME.zip and $OUT/latest.json"
