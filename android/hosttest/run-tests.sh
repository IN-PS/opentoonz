#!/usr/bin/env bash
# Runs the Android app (Activities, views, dialogs, file I/O) on the JVM with
# Robolectric, wired through JNI to the native OpenToonz engine built for the
# host.  Usage: android/hosttest/run-tests.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/..
OUT=$ROOT/out/hosttest
mkdir -p "$OUT"
# 1. native engine for the host (same sources as the APK)
#    with MyPaint (sources fetched once, checksum-verified)
MYPAINT_SRC=${MYPAINT_SRC:-$OUT/mypaint-src}
bash "$ROOT/tools/fetch-mypaint.sh" "$MYPAINT_SRC" >/dev/null
cmake -S "$ROOT/engine" -B "$OUT/native" -G Ninja -DCMAKE_BUILD_TYPE=Release -DOTM_BUILD_JNI=ON \
  -DOTM_MYPAINT_DIR="$MYPAINT_SRC/libmypaint-1.6.0" \
  -DOTM_JSONC_DIR="$MYPAINT_SRC/json-c-json-c-0.17-20230812" >/dev/null
cmake --build "$OUT/native" --target otmengine lzocompress lzodecompress otm_tests
# 2. resources (aapt2) + R.java exactly as for the APK
SDK=${ANDROID_SDK_ROOT:-/usr/lib/android-sdk}
BT=$(ls -d "$SDK"/build-tools/[0-9]*/ | sort -V | tail -1)
JAR=$(ls -d "$SDK"/platforms/android-*/android.jar | sort -V | tail -1)
"$BT/aapt2" compile --dir "$ROOT/app/src/main/res" -o "$OUT/res.zip"
rm -rf "$OUT/gen"; mkdir -p "$OUT/gen"
"$BT/aapt2" link -I "$JAR" --manifest "$ROOT/app/src/main/AndroidManifest.xml" \
  --min-sdk-version 26 --target-sdk-version 28 --java "$OUT/gen" -o "$OUT/base.apk" "$OUT/res.zip"
# 3. Robolectric tests
GRADLE=${GRADLE:-$(command -v gradle || echo /opt/gradle-8.14.3/bin/gradle)}
"$GRADLE" -p "$HERE" --no-daemon test "$@"
