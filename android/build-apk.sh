#!/usr/bin/env bash
# OpenToonz Mobile - APK build without Gradle (reproducible, scriptable).
#
#   android/build-apk.sh [debug|release]
#
# Steps:
#   1. native: CMake + Android NDK  -> libotmengine.so, liblzo*.so, Qt5Core, libc++
#   2. resources: aapt2 compile/link
#   3. java: javac (Java 8 language level, no lambdas) + dx/d8
#   4. package, zipalign, apksigner
#
# Environment (defaults in brackets):
#   ANDROID_SDK_ROOT  [/usr/lib/android-sdk]   needs platforms/android-$ANDROID_PLATFORM
#   ANDROID_PLATFORM  [auto: highest installed]
#   BUILD_TOOLS       [auto: highest in $ANDROID_SDK_ROOT/build-tools]
#   ANDROID_NDK_ROOT  (required unless SKIP_NATIVE=1)
#   QT5_ANDROID       Qt 5.15 built for android arm64 (prefix containing lib/cmake/Qt5)
#   HOST_MOC          [moc from $QT5_ANDROID/bin or /usr/lib/qt5/bin/moc]  (host binary)
#   BOOST_INCLUDE     [/usr/include]   header-only boost
#   KEYSTORE / KEYSTORE_PASS / KEY_ALIAS   release signing (debug: auto keystore)
#   SKIP_NATIVE=1     package only Java/resources (APK will not run; CI smoke test)
set -euo pipefail
VARIANT=${1:-debug}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/out/$VARIANT}
SDK=${ANDROID_SDK_ROOT:-/usr/lib/android-sdk}
ABI=arm64-v8a
MIN_API=26

if [ -z "${ANDROID_PLATFORM:-}" ]; then
  ANDROID_PLATFORM=$(ls "$SDK/platforms" | sed -n 's/^android-\([0-9]*\)$/\1/p' | sort -n | tail -1)
fi
ANDROID_JAR=$SDK/platforms/android-$ANDROID_PLATFORM/android.jar
if [ -z "${BUILD_TOOLS:-}" ]; then
  BUILD_TOOLS=$(ls -d "$SDK"/build-tools/[0-9]*/ | sort -V | tail -1)
fi
AAPT2=$BUILD_TOOLS/aapt2
ZIPALIGN=$BUILD_TOOLS/zipalign
APKSIGNER=$BUILD_TOOLS/apksigner
if [ -x "$BUILD_TOOLS/d8" ]; then DEXER=d8; else DEXER=dx; fi
for f in "$ANDROID_JAR" "$AAPT2" "$ZIPALIGN" "$APKSIGNER"; do
  [ -e "$f" ] || { echo "missing: $f"; exit 1; }
done
echo "== SDK platform android-$ANDROID_PLATFORM, build-tools $(basename "$BUILD_TOOLS"), dexer $DEXER"

rm -rf "$OUT"; mkdir -p "$OUT"/{native,res,gen,classes,apk/lib/$ABI}

# ------------------------------------------------------------------ 1. native
if [ "${SKIP_NATIVE:-0}" != 1 ]; then
  : "${ANDROID_NDK_ROOT:?set ANDROID_NDK_ROOT}"
  : "${QT5_ANDROID:?set QT5_ANDROID (Qt 5.15 for Android arm64-v8a)}"
  HOST_MOC=${HOST_MOC:-}
  [ -z "$HOST_MOC" ] && [ -x "$QT5_ANDROID/bin/moc" ] && HOST_MOC=$QT5_ANDROID/bin/moc
  [ -z "$HOST_MOC" ] && HOST_MOC=/usr/lib/qt5/bin/moc
  BT=Release; [ "$VARIANT" = debug ] && BT=RelWithDebInfo
  cmake -S "$HERE/engine" -B "$OUT/native" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-$MIN_API -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE=$BT -DOTM_BUILD_TESTS=OFF \
    -DQt5_DIR="$QT5_ANDROID/lib/cmake/Qt5" -DQt5Core_DIR="$QT5_ANDROID/lib/cmake/Qt5Core" \
    -DCMAKE_FIND_ROOT_PATH="$QT5_ANDROID" -DOTM_HOST_MOC="$HOST_MOC" \
    -DOTM_BOOST_INCLUDE="${BOOST_INCLUDE:-/usr/include}" \
    -DANDROID_SDK="$SDK" -DANDROID_SDK_BUILD_TOOLS_REVISION="$(basename "$BUILD_TOOLS")"
  cmake --build "$OUT/native" --target otmengine lzocompress lzodecompress
  L=$OUT/apk/lib/$ABI
  cp "$OUT/native/libotmengine.so" "$L/"
  # helper executables must be named lib*.so to be installed with the app
  cp "$OUT/native/lzocompress" "$L/liblzocompress.so"
  cp "$OUT/native/lzodecompress" "$L/liblzodecompress.so"
  cp "$QT5_ANDROID/lib/libQt5Core_$ABI.so" "$L/" 2>/dev/null || cp "$QT5_ANDROID/lib/libQt5Core.so" "$L/"
  STL=$(find "$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt" -path "*aarch64-linux-android/libc++_shared.so" | head -1)
  cp "$STL" "$L/"
  STRIP=$(ls "$ANDROID_NDK_ROOT"/toolchains/llvm/prebuilt/*/bin/llvm-strip | head -1)
  [ "$VARIANT" = release ] && for so in "$L"/*.so; do "$STRIP" --strip-unneeded "$so"; done
  # every DT_NEEDED must be a system library or shipped in the APK
  READELF=$(ls "$ANDROID_NDK_ROOT"/toolchains/llvm/prebuilt/*/bin/llvm-readelf | head -1)
  for so in "$L"/*.so; do
    for dep in $("$READELF" -d "$so" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p'); do
      case "$dep" in
        libc.so|libm.so|libdl.so|liblog.so|libandroid.so|libjnigraphics.so|libz.so|libEGL.so|libGLESv2.so|libmediandk.so) ;;
        *) [ -e "$L/$dep" ] || { echo "unresolved dependency $dep of $(basename "$so")"; exit 1; } ;;
      esac
    done
  done
else
  echo "== SKIP_NATIVE=1: APK without native libraries (will not run)"
fi

# ------------------------------------------------------------------ 2. resources
APP=$HERE/app/src/main
"$AAPT2" compile --dir "$APP/res" -o "$OUT/res/res.zip"
VERSION_NAME=$(sed -n 's/.*android:versionName="\([^"]*\)".*/\1/p' "$APP/AndroidManifest.xml")
"$AAPT2" link -I "$ANDROID_JAR" --manifest "$APP/AndroidManifest.xml" \
  --min-sdk-version $MIN_API --target-sdk-version 28 \
  --java "$OUT/gen" -o "$OUT/base.apk" "$OUT/res/res.zip" --auto-add-overlay

# ------------------------------------------------------------------ 3. java
find "$APP/java" "$OUT/gen" -name '*.java' > "$OUT/sources.txt"
javac -nowarn -Xlint:-options --release 8 -encoding UTF-8 -classpath "$ANDROID_JAR" \
  -d "$OUT/classes" @"$OUT/sources.txt"
if [ $DEXER = d8 ]; then
  "$BUILD_TOOLS/d8" --min-api $MIN_API --lib "$ANDROID_JAR" --output "$OUT" \
    $( [ "$VARIANT" = release ] && echo --release ) $(find "$OUT/classes" -name '*.class')
else
  "$BUILD_TOOLS/dx" --dex --min-sdk-version=$MIN_API --output="$OUT/classes.dex" "$OUT/classes"
fi

# ------------------------------------------------------------------ 4. package & sign
cp "$OUT/base.apk" "$OUT/unsigned.apk"
(cd "$OUT" && zip -q -j unsigned.apk classes.dex)
# native libs stored uncompressed is not required (extractNativeLibs=true)
(cd "$OUT/apk" && [ -d lib ] && zip -q -r ../unsigned.apk lib || true)
"$ZIPALIGN" -f -p 4 "$OUT/unsigned.apk" "$OUT/aligned.apk"

if [ "$VARIANT" = release ]; then
  : "${KEYSTORE:?set KEYSTORE/KEYSTORE_PASS/KEY_ALIAS for release signing}"
  KS=$KEYSTORE; PASS=$KEYSTORE_PASS; ALIAS=${KEY_ALIAS:-release}
else
  KS=$HERE/out/debug.keystore; PASS=android; ALIAS=androiddebugkey
  [ -f "$KS" ] || keytool -genkeypair -keystore "$KS" -storepass $PASS -keypass $PASS \
      -alias $ALIAS -keyalg RSA -keysize 2048 -validity 10000 \
      -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1
fi
APK=$OUT/OpenToonzMobile-$VERSION_NAME-$VARIANT.apk
"$APKSIGNER" sign --ks "$KS" --ks-pass pass:$PASS --key-pass pass:$PASS --ks-key-alias $ALIAS \
  --min-sdk-version $MIN_API --out "$APK" "$OUT/aligned.apk"
"$APKSIGNER" verify --min-sdk-version $MIN_API "$APK"
echo "== built $APK"
unzip -l "$APK" | tail -n +1 | grep -E "lib/|classes.dex|AndroidManifest" || true
