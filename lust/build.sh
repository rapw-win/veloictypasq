#!/bin/sh
# Builds release/lust.apk with the Android SDK command-line tools only (no Gradle):
#   NDK clang -> liblust.so (arm64-v8a, armeabi-v7a), aapt2 -> resources, javac + d8 -> classes.dex,
#   zipalign + apksigner (debug keystore, generated on first run).
# Environment: ANDROID_SDK (default /opt/android-sdk), NDK_VER (26.3.11579264), BT_VER (36.0.0), API (34), ABIS.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SDK=${ANDROID_SDK:-/opt/android-sdk}
NDK=$SDK/ndk/${NDK_VER:-26.3.11579264}/toolchains/llvm/prebuilt/linux-x86_64/bin
BT=$SDK/build-tools/${BT_VER:-36.0.0}
JAR=$SDK/platforms/android-${API:-34}/android.jar
ABIS=${ABIS:-"arm64-v8a armeabi-v7a"}
B=$HERE/build/android
OUT=$HERE/release/lust.apk

"$HERE/tools/fetch_velocity.sh"
mkdir -p "$B" "$HERE/release"

# ---- native library
SRC_ASTRAL="draw icons ui preview persist menu pages"
SRC_IMGUI="imgui imgui_draw imgui_tables imgui_widgets"
for abi in $ABIS; do
  case $abi in
    arm64-v8a) CXX=$NDK/aarch64-linux-android26-clang++ ;;
    armeabi-v7a) CXX=$NDK/armv7a-linux-androideabi26-clang++ ;;
    x86_64) CXX=$NDK/x86_64-linux-android26-clang++ ;;
    *) echo "unknown abi $abi"; exit 1 ;;
  esac
  O=$B/obj/$abi; L=$B/apk/lib/$abi; mkdir -p "$O" "$L"
  FLAGS="-O2 -std=c++17 -fPIC -w -ffunction-sections -fdata-sections -I$HERE/src -I$HERE/third_party/imgui -I$HERE/build/velocity"
  [ -f "$O/model3d.o" ] || $CXX $FLAGS -c "$HERE/build/velocity/model3d.cpp" -o "$O/model3d.o" &
  for f in $SRC_IMGUI; do [ -f "$O/$f.o" ] || $CXX $FLAGS -c "$HERE/third_party/imgui/$f.cpp" -o "$O/$f.o" & done
  [ -f "$O/gl3.o" ] || $CXX $FLAGS -DIMGUI_IMPL_OPENGL_ES3 -c "$HERE/third_party/imgui/backends/imgui_impl_opengl3.cpp" -o "$O/gl3.o" &
  for f in $SRC_ASTRAL; do $CXX $FLAGS -c "$HERE/src/astral/$f.cpp" -o "$O/a_$f.o" & done
  $CXX $FLAGS -DIMGUI_IMPL_OPENGL_ES3 -c "$HERE/src/android_main.cpp" -o "$O/main.o" &
  wait
  $CXX -shared -static-libstdc++ -Wl,--gc-sections -Wl,--no-undefined -Wl,-z,max-page-size=16384 -o "$L/liblust.so" "$O"/*.o \
       -landroid -lEGL -lGLESv3 -llog -lm
  "$NDK/llvm-strip" --strip-unneeded "$L/liblust.so"
  echo "built $abi: $(du -h "$L/liblust.so" | cut -f1)"
done

# ---- resources + java
rm -rf "$B/res" "$B/gen" "$B/classes" "$B/dex"; mkdir -p "$B/res" "$B/gen" "$B/classes" "$B/dex" "$B/apk/assets"
"$BT/aapt2" compile --dir "$HERE/android/res" -o "$B/res/res.zip"
"$BT/aapt2" link -o "$B/base.apk" -I "$JAR" --manifest "$HERE/android/AndroidManifest.xml" --java "$B/gen" "$B/res/res.zip" \
    --min-sdk-version 26 --target-sdk-version 30 --version-code 1 --version-name 1.0
javac -nowarn --release 8 -classpath "$JAR" -d "$B/classes" \
    -Xlint:-options $(find "$HERE/android/src" "$B/gen" -name '*.java')
"$BT/d8" --min-api 26 --output "$B/dex" --lib "$JAR" $(find "$B/classes" -name '*.class')

# ---- package: dex, native libs, license notices
cp "$HERE/LICENSE-Astral-Remake.txt" "$HERE/THIRD_PARTY_NOTICES.md" "$B/apk/assets/"
cp "$B/base.apk" "$B/unsigned.apk"
cp "$B/dex/classes.dex" "$B/apk/"
(cd "$B/apk" && zip -q -r "$B/unsigned.apk" classes.dex lib assets)
"$BT/zipalign" -f -p 4 "$B/unsigned.apk" "$B/aligned.apk"

KS=$HERE/android/debug.keystore   # throwaway debug key, kept so rebuilt APKs install as updates
[ -f "$KS" ] || keytool -genkeypair -keystore "$KS" -storepass android -keypass android -alias lust -keyalg RSA -keysize 2048 \
    -validity 10000 -dname "CN=lust" >/dev/null 2>&1
"$BT/apksigner" sign --v4-signing-enabled false --ks "$KS" --ks-pass pass:android --key-pass pass:android --ks-key-alias lust --out "$OUT" "$B/aligned.apk"
"$BT/apksigner" verify "$OUT"
echo "APK: $OUT ($(du -h "$OUT" | cut -f1))"
