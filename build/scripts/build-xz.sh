#!/usr/bin/env bash
# build-xz.sh — Cross-compile XZ utils for KratosOS (Phase 2, step 12)
# Provides: xz, lzma, unxz, xzcat

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="xz"
VERSION="$XZ_VERSION"
ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.xz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BUILD_DIR="$KRATOS_WORK/$PACKAGE-build"
URL="https://github.com/tukaani-project/xz/releases/download/v${VERSION}/xz-${VERSION}.tar.xz"

CROSS="$KRATOS_TOOLS/bin/$TARGET"
SYSROOT="$KRATOS_SYSROOT"

echo "========================================"
echo "          KRATOSOS XZ $VERSION"
echo "========================================"
mkdir -p "$KRATOS_DOWNLOADS" "$KRATOS_SOURCES" "$KRATOS_WORK"

if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading xz $VERSION..."
    curl -L "$URL" -o "$ARCHIVE"
fi
if [ ! -d "$SOURCE_DIR" ]; then tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"; fi

rm -rf "$BUILD_DIR"; mkdir -p "$BUILD_DIR"; cd "$BUILD_DIR"

"$SOURCE_DIR/configure" \
    --host="$TARGET" \
    --prefix=/usr \
    --disable-nls \
    --disable-doc \
    CC="${CROSS}-gcc" \
    CFLAGS="--sysroot=$SYSROOT -I$SYSROOT/usr/include" \
    LDFLAGS="--sysroot=$SYSROOT -L$SYSROOT/usr/lib"

make -j"$(nproc)"
make DESTDIR="$SYSROOT" install

echo "[✓] xz $VERSION installed into sysroot."
