#!/bin/sh
# Packs a private release: blend/out/picori-seal-vX.Y.zip plus latest.json, the
# manifest the game's auto updaters read (version, file, sha256). When the Android
# app is built (blend/out/picori-seal.apk) it goes alongside as
# picori-seal-vX.Y.apk, listed as apk, apk_sha256 and apk_version. Android takes
# just the game file from the zip and only installs the APK when it is newer.
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
APK=
if [ -f "$BLEND/out/picori-seal.apk" ]; then
    cp "$BLEND/out/picori-seal.apk" "$OUT/$NAME.apk"
    APKSHA=$(sha256sum "$OUT/$NAME.apk" | cut -d' ' -f1)
    APK=$(printf ',\n  "apk": "%s.apk",\n  "apk_sha256": "%s",\n  "apk_version": "%s"' "$NAME" "$APKSHA" "$VERSION")
fi
printf '{\n  "version": "%s",\n  "file": "%s.zip",\n  "sha256": "%s"%s\n}\n' "$VERSION" "$NAME" "$SHA" "$APK" > "$OUT/latest.json"
echo "Packed $OUT/$NAME.zip${APK:+, $NAME.apk} and $OUT/latest.json"
