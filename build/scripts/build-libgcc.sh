#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="gcc"
VERSION="$GCC_VERSION"

# Reuse the GCC source tree already extracted for pass 1 — no re-download.
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BUILD_DIR="$KRATOS_WORK/$PACKAGE-libgcc-build"

echo "================================"
echo "        KRATOSOS LIBGCC"
echo "================================"
echo
echo "Target:  $TARGET"
echo "Version: $VERSION"
echo

if [ ! -d "$SOURCE_DIR" ]; then
    echo "[!] GCC sources not found at $SOURCE_DIR"
    echo "    Run build-gcc-pass1.sh first."
    exit 1
fi

for f in crt1.o crti.o crtn.o libc.so; do
    if [ ! -f "$KRATOS_SYSROOT/usr/lib/$f" ]; then
        echo "[!] Missing $KRATOS_SYSROOT/usr/lib/$f"
        echo "    Run build-glibc-bootstrap.sh first."
        exit 1
    fi
done

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"

echo "[+] Configuring libgcc..."

# Same as GCC pass 1, but WITHOUT --disable-shared: now that the sysroot
# has crt*.o and a (stub) libc.so, GCC can link libgcc_s.so. This build
# dir is separate from the pass 1 one on purpose, since the configure
# flags differ (shared vs static-only).
"$SOURCE_DIR/configure" \
    --target="$TARGET" \
    --prefix="$KRATOS_TOOLS" \
    --with-sysroot="$KRATOS_SYSROOT" \
    --disable-nls \
    --disable-multilib \
    --disable-threads \
    --disable-libatomic \
    --disable-libgomp \
    --disable-libquadmath \
    --disable-libssp \
    --disable-libvtv \
    --disable-libstdcxx \
    --disable-libsanitizer \
    --disable-libcody \
    --enable-languages=c \
    --enable-shared

echo "[+] Building libgcc..."

make all-target-libgcc -j"$(nproc)"

echo "[+] Installing libgcc..."

make install-target-libgcc

echo
echo "[+] libgcc installed successfully."
echo
echo "Installed into:"
echo "  $KRATOS_TOOLS/lib/gcc/$TARGET/$VERSION/"
echo "  $KRATOS_TOOLS/$TARGET/lib/ (libgcc_s.so)"
echo
echo "Next step: build-glibc-full.sh (full glibc build, using this libgcc)"
