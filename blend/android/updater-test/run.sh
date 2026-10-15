#!/bin/sh
# Tests the Android updater's logic on a desktop JVM against stand-in servers
# (a latest.json host and a GitHub releases API that redirects downloads).
#   ./blend/android/updater-test/run.sh path/to/picori-seal-vX.Y.zip   (a real 1.2 build zip)
# Needs a JDK and Robolectric's android-all jar (Maven Central) for the framework classes.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ZIP=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
WORK=$(mktemp -d)
JAR=${ANDROID_ALL:-$WORK/android-all.jar}
[ -f "$JAR" ] || curl -sSfL -o "$JAR" \
    https://repo1.maven.org/maven2/org/robolectric/android-all/13-robolectric-9030017/android-all-13-robolectric-9030017.jar
mkdir -p "$WORK/rel" "$WORK/classes"
cp "$ZIP" "$WORK/rel/picori-seal-v1.2.zip"
SHA=$(sha256sum "$WORK/rel/picori-seal-v1.2.zip" | cut -d' ' -f1)
printf '{"version":"1.2","file":"picori-seal-v1.2.zip","sha256":"%s"}' "$SHA" > "$WORK/rel/latest.json"
printf '{"version":"1.2","file":"picori-seal-v1.2.zip","sha256":"%s","apk":"picori-seal-v1.2.apk","apk_sha256":"00"}' "$SHA" > "$WORK/rel/withapk.json"
python3 -I "$HERE/manifest_server.py" "$WORK/rel" > "$WORK/m.log" 2>&1 & M=$!
python3 -I "$HERE/github_server.py" "$WORK/rel/picori-seal-v1.2.zip" > "$WORK/g.log" 2>&1 & G=$!
trap 'kill $M $G' EXIT
sleep 1
javac -nowarn --release 11 -cp "$JAR" -d "$WORK/classes" "$HERE/../src/com/picoriseal/game/Updater.java" "$HERE/UpdaterTest.java"
java -cp "$JAR:$WORK/classes" com.picoriseal.game.UpdaterTest "$WORK/files"
