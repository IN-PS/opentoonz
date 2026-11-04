#!/usr/bin/env bash
# Replaces the host executables of an Android NDK tree with thin wrappers around
# a system LLVM 18 toolchain (clang/lld/llvm-*), keeping the NDK's sysroot,
# libc++ and compiler-rt.  Only needed when the official Google NDK host
# binaries cannot be used (e.g. a sandbox where dl.google.com is blocked and
# the NDK tree came from a third-party mirror).  With an official NDK you do
# NOT need this script.
#
# usage: make-hybrid-ndk.sh <ndk-root> [llvm-bin-dir]
set -euo pipefail
NDK=$(readlink -f "$1")
LLVM=${2:-/usr/lib/llvm-18/bin}
P=$NDK/toolchains/llvm/prebuilt/linux-x86_64
RES=$P/lib/clang/18
[ -d "$RES" ] || { echo "no clang 18 resource dir in NDK: $RES"; exit 1; }
[ -d "$P/bin.thirdparty" ] || mv "$P/bin" "$P/bin.thirdparty"
rm -rf "$P/bin"; mkdir -p "$P/bin"
COMMON='--sysroot='"$P/sysroot"' -resource-dir='"$RES"' -fuse-ld=lld -rtlib=compiler-rt -unwindlib=libunwind -Wno-unused-command-line-argument'
for tool in clang clang++; do
  cat > "$P/bin/$tool" <<W
#!/bin/sh
exec "$LLVM/$tool" $COMMON "\$@"
W
done
ln -sf clang "$P/bin/clang-18"
for t in ld.lld lld llvm-ar llvm-ranlib llvm-strip llvm-objcopy llvm-objdump llvm-nm \
         llvm-readelf llvm-readobj llvm-size llvm-strings llvm-symbolizer llvm-addr2line \
         llvm-as llvm-dwp llvm-cxxfilt; do
  ln -sf "$LLVM/$t" "$P/bin/$t"
done
ln -sf ld.lld "$P/bin/ld"
for api in $(seq 21 35); do
  for tc in aarch64-linux-android x86_64-linux-android; do
    for cc in clang clang++; do
      cat > "$P/bin/$tc$api-$cc" <<W
#!/bin/sh
exec "$P/bin/$cc" --target=$tc$api "\$@"
W
    done
  done
done
chmod +x "$P/bin/"*
echo "hybrid NDK ready: $NDK"
