#!/usr/bin/env bash
# Fetches the libmypaint 1.6.0 + json-c 0.17 sources (Ubuntu archive orig
# tarballs, checksum-verified) used for the MyPaint brushes.
#   tools/fetch-mypaint.sh <dest-dir>
# then configure the engine with -DOTM_MYPAINT_DIR=<dest>/libmypaint-1.6.0
#                               -DOTM_JSONC_DIR=<dest>/json-c-json-c-0.17-20230812
set -euo pipefail
D=$(mkdir -p "$1" && cd "$1" && pwd)
U=https://archive.ubuntu.com/ubuntu/pool
cd "$D"
fetch() { [ -f "$2" ] || curl -sSfL -o "$2" "$1/$2"; echo "$3  $2" | sha256sum -c -; }
fetch $U/universe/libm/libmypaint libmypaint_1.6.0.orig.tar.xz \
  a5ec3624ba469b7b35fd66b6fbee7f07285b7a7813d02291ac9b10e46618140e
fetch $U/main/j/json-c json-c_0.17.orig.tar.gz \
  024d302a3aadcbf9f78735320a6d5aedf8b77876c8ac8bbb95081ca55054c7eb
[ -d libmypaint-1.6.0 ] || tar xf libmypaint_1.6.0.orig.tar.xz
[ -d json-c-json-c-0.17-20230812 ] || tar xzf json-c_0.17.orig.tar.gz
echo "OTM_MYPAINT_DIR=$D/libmypaint-1.6.0"
echo "OTM_JSONC_DIR=$D/json-c-json-c-0.17-20230812"
