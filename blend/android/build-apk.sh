#!/bin/sh
# Builds the Android version of the co-op player as an arm64 APK with the game
# inside it. Made for the AYN Odin 2 Portal, works on any arm64 Android 8+ device.
#
#   blend/android/build-apk.sh path/to/zelda-blend.gba [out.apk]
#
# Needs a JDK (17+), cmake and ninja, plus:
#   ANDROID_HOME       Android SDK with platforms;android-35 and build-tools (35+)
#   ANDROID_NDK_HOME   Android NDK (r26 or newer)
#   SDL                SDL 2.30.x source (default blend/host/deps/SDL)
#   MGBA               mGBA 0.10.x source (default blend/host/deps/mgba)
#   KEYSTORE           signing key, created on first use (default ~/.android/picori-seal.keystore).
#                      Keep it: Android only installs an update over the old APK when
#                      both are signed with the same key, and uninstalling deletes the saves.
#   KEYSTORE_PASS      its password (default picoriseal)
#   VERSION_NAME, VERSION_CODE   app version (default blend/VERSION; VERSION_CODE must go
#                                up with each release, e.g. 110 for 1.1, 120 for 1.2)
#
# The APK holds the game, which is built from your own ROM: keep it private.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
HOST=$HERE/../host
ROM=${1:?usage: build-apk.sh zelda-blend.gba [out.apk]}
OUT=${2:-$HERE/../out/picori-seal.apk}
SDL=${SDL:-$HOST/deps/SDL}
MGBA=${MGBA:-$HOST/deps/mgba}
NDK=${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME}
SDK=${ANDROID_HOME:?set ANDROID_HOME}
KEYSTORE=${KEYSTORE:-$HOME/.android/picori-seal.keystore}
KEYSTORE_PASS=${KEYSTORE_PASS:-picoriseal}
VERSION_NAME=${VERSION_NAME:-$(cat "$HERE/../VERSION")}
VERSION_CODE=${VERSION_CODE:-1}
ABI=arm64-v8a
API=26
BUILD=$HERE/build
BT=$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)
JAR=$SDK/platforms/android-35/android.jar
TOOLCHAIN=$NDK/build/cmake/android.toolchain.cmake
CC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android$API-clang
mkdir -p "$BUILD" "$(dirname "$OUT")"

echo "== SDL2"
if [ ! -f "$BUILD/sdl/libSDL2.so" ]; then
	cmake -S "$SDL" -B "$BUILD/sdl" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DANDROID_ABI=$ABI \
		-DANDROID_PLATFORM=android-$API -DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF \
		-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384 >/dev/null
	ninja -C "$BUILD/sdl" >/dev/null
fi

echo "== mGBA"
if [ ! -f "$BUILD/mgba/libmgba.a" ]; then
	cmake -S "$MGBA" -B "$BUILD/mgba" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DANDROID_ABI=$ABI \
		-DANDROID_PLATFORM=android-$API -DCMAKE_BUILD_TYPE=Release -DLIBMGBA_ONLY=ON -DBUILD_STATIC=ON \
		-DBUILD_SHARED=OFF -DM_CORE_GB=OFF -DUSE_ZLIB=OFF -DUSE_PNG=OFF -DUSE_LIBZIP=OFF -DUSE_MINIZIP=OFF \
		-DUSE_SQLITE3=OFF -DUSE_ELF=OFF -DUSE_LZMA=OFF -DUSE_EDITLINE=OFF -DUSE_FFMPEG=OFF -DUSE_EPOXY=OFF \
		-DUSE_DISCORD_RPC=OFF -DENABLE_SCRIPTING=OFF -DUSE_DEBUGGERS=OFF -DUSE_GDB_STUB=OFF >/dev/null
	ninja -C "$BUILD/mgba" >/dev/null
fi

echo "== host (libmain.so)"
LIB=$BUILD/apk/lib/$ABI
rm -rf "$BUILD/apk" && mkdir -p "$LIB" "$BUILD/assets"
"$CC" -O2 -Wall -fPIC -shared -o "$LIB/libmain.so" "$HOST/coop.c" "$HOST/android.c" "$HOST/updater.c" \
	-DPICORI_VERSION="\"$VERSION_NAME\"" \
	-I"$SDL/include" -I"$BUILD/sdl/include-config-release/SDL2" -I"$MGBA/include" -I"$BUILD/mgba/include" \
	-DMGBA_STANDALONE "$BUILD/mgba/libmgba.a" -L"$BUILD/sdl" -lSDL2 -llog -landroid -lm \
	-Wl,-z,max-page-size=16384 -Wl,--no-undefined
cp "$BUILD/sdl/libSDL2.so" "$LIB/"

echo "== Java"
rm -rf "$BUILD/classes" "$BUILD/dex" && mkdir -p "$BUILD/classes" "$BUILD/dex"
javac -nowarn -Xlint:-options -source 11 -target 11 -encoding UTF-8 -classpath "$JAR" -d "$BUILD/classes" \
	"$SDL"/android-project/app/src/main/java/org/libsdl/app/*.java "$HERE"/java/com/picoriseal/game/*.java
"$BT/d8" --release --min-api $API --lib "$JAR" --output "$BUILD/dex" $(find "$BUILD/classes" -name '*.class')

echo "== resources and packaging"
sed -e "s/@VERSION_CODE@/$VERSION_CODE/" -e "s/@VERSION_NAME@/$VERSION_NAME/" "$HERE/AndroidManifest.xml" \
	> "$BUILD/AndroidManifest.xml"
cp "$ROM" "$BUILD/assets/zelda-blend.gba"
"$BT/aapt2" compile --dir "$HERE/res" -o "$BUILD/res.zip"
"$BT/aapt2" link -o "$BUILD/unsigned.apk" -I "$JAR" --manifest "$BUILD/AndroidManifest.xml" \
	-A "$BUILD/assets" -0 gba "$BUILD/res.zip"
cp "$BUILD/dex/classes.dex" "$BUILD/apk/"
(cd "$BUILD/apk" && zip -q -r "$BUILD/unsigned.apk" classes.dex lib)
"$BT/zipalign" -f -P 16 4 "$BUILD/unsigned.apk" "$BUILD/aligned.apk"

if [ ! -f "$KEYSTORE" ]; then
	echo "== creating signing key $KEYSTORE"
	mkdir -p "$(dirname "$KEYSTORE")"
	keytool -genkeypair -keystore "$KEYSTORE" -storepass "$KEYSTORE_PASS" -keypass "$KEYSTORE_PASS" \
		-alias picori -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Picori Seal" >/dev/null 2>&1
fi
"$BT/apksigner" sign --ks "$KEYSTORE" --ks-pass "pass:$KEYSTORE_PASS" --ks-key-alias picori \
	--out "$OUT" "$BUILD/aligned.apk"
"$BT/apksigner" verify "$OUT"
echo "Built $OUT"
