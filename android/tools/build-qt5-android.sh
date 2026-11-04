#!/usr/bin/env bash
# Builds Qt 5.15 QtCore (no GUI, no widgets) for Android arm64-v8a, which is
# the only Qt module the OpenToonz Mobile engine links (tnzcore/toonzlib use
# QString, QDir, QProcess, QMutex, QObject ...).
#
#   tools/build-qt5-android.sh <qtbase-src-dir> <install-prefix>
#
# Needs: ANDROID_NDK_ROOT (r21+; tested configuration: r27c), ANDROID_SDK_ROOT
# (any SDK with a platforms/android-XX directory), host g++ and make.
# qtbase sources: https://download.qt.io/archive/qt/5.15/  (qtbase-everywhere-src-5.15.x)
# or the Debian/Ubuntu "qtbase-opensource-src" orig tarball.
#
# Used to build the Qt5Core of the v0.1.0 APKs (qtbase 5.15.13, NDK r27c).
set -euo pipefail
SRC=$(readlink -f "$1")
PREFIX=$(readlink -f -m "$2")
BUILD=${BUILD_DIR:-$PWD/qt5-android-build}
: "${ANDROID_NDK_ROOT:?}"
: "${ANDROID_SDK_ROOT:?}"
# NDK r23+ ships <execinfo.h> but backtrace() only exists from API 33:
# keep Qt from using it on Android (same fix as later Qt releases).
sed -i 's|__has_include(<execinfo.h>))$|__has_include(<execinfo.h>) \&\& !defined(__ANDROID__))|' \
  "$SRC/src/corelib/global/qlogging.cpp"
mkdir -p "$BUILD"
cd "$BUILD"
"$SRC/configure" -xplatform android-clang \
  -android-ndk "$ANDROID_NDK_ROOT" -android-sdk "$ANDROID_SDK_ROOT" \
  -android-abis arm64-v8a -android-ndk-platform android-26 \
  -opensource -confirm-license -release -prefix "$PREFIX" \
  -nomake tests -nomake examples -no-gui -no-widgets -no-dbus -no-opengl \
  -no-ssl -no-icu -qt-pcre -system-zlib \
  -no-feature-concurrent -no-feature-sql -no-feature-network \
  -no-feature-testlib -no-feature-xml -silent
make qmake_all || true   # src/android (java parts) may fail; not needed
make -j"$(nproc)" -C src sub-corelib
make -C src/corelib install
# host tools (moc) are built for the build machine; install them too
mkdir -p "$PREFIX/bin"
cp bin/moc "$PREFIX/bin/" 2>/dev/null || true
command -v rcc >/dev/null && cp "$(command -v rcc)" "$PREFIX/bin/" || true
make install_mkspecs
ls "$PREFIX/lib" | grep -i qt5core
echo "Qt5Core for Android installed in $PREFIX"
echo "use: QT5_ANDROID=$PREFIX android/build-apk.sh debug"
