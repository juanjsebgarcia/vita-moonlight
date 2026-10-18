#!/bin/sh
# Install a faster libcrypto.a over the SDK's. Runs inside the VitaSDK image,
# from the repository root, after the openssl (1.0.2) vdpm package is in place.
#
# The package builds vita-openssl with an empty CFLAGS field (so -O0) and no
# assembly, and AES-128-GCM then runs at about 1.3 MB/s on the Vita: too slow
# to decrypt a video stream. The fork below builds the same OpenSSL 1.0.2 at
# -O2 with the ARM/NEON assembly, about 9x faster. Only libcrypto.a is
# replaced; libssl and the headers stay the package's, which match.
#
# The library is built once per commit and kept in build/, so later builds
# only copy it. Delete build/libcrypto-*.a to build it again.

set -eu

OPENSSL_REPO="https://github.com/juanjsebgarcia/vita-openssl.git"
# Branch vita-neon; see https://github.com/juanjsebgarcia/vita-openssl/pull/1
OPENSSL_COMMIT="b9dbf813932e192f507457e6bdca54814cc8da49"

lib="build/libcrypto-$OPENSSL_COMMIT.a"

if [ ! -f "$lib" ]; then
  echo "building libcrypto from vita-openssl $OPENSSL_COMMIT"
  src="$(mktemp -d)"
  git -C "$src" init -q
  git -C "$src" fetch -q --depth 1 "$OPENSSL_REPO" "$OPENSSL_COMMIT"
  git -C "$src" checkout -q FETCH_HEAD
  # make depend is skipped: it needs makedepend, which the image lacks, and
  # a one-off build doesn't need dependency tracking
  (cd "$src" && ./Configure no-threads vita-cross >/dev/null && make -j"$(nproc)" build_libs >"$src/build.log" 2>&1) \
    || { tail -n 30 "$src/build.log"; exit 1; }
  mkdir -p build
  # Through a temporary name, so an interrupted build isn't cached
  cp "$src/libcrypto.a" "$lib.tmp"
  mv "$lib.tmp" "$lib"
  rm -rf "$src"
fi

cp "$lib" "$VITASDK/arm-vita-eabi/lib/libcrypto.a"
