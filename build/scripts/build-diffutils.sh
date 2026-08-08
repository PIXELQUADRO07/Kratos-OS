#!/usr/bin/env bash
# build-diffutils.sh — Cross-compile diffutils for KratosOS (Phase 2, step 9)
# Provides: diff, cmp, diff3, sdiff

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="diffutils"
VERSION="$DIFFUTILS_VERSION"
ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.xz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BUILD_DIR="$KRATOS_WORK/$PACKAGE-build"
URL="https://ftp.gnu.org/gnu/diffutils/diffutils-$VERSION.tar.xz"

CROSS="$KRATOS_TOOLS/bin/$TARGET"
SYSROOT="$KRATOS_SYSROOT"

echo "========================================"
echo "      KRATOSOS DIFFUTILS $VERSION"
echo "========================================"
mkdir -p "$KRATOS_DOWNLOADS" "$KRATOS_SOURCES" "$KRATOS_WORK"

if [ ! -f "$ARCHIVE" ]; then curl -L "$URL" -o "$ARCHIVE"; fi
if [ ! -d "$SOURCE_DIR" ]; then tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"; fi

rm -rf "$BUILD_DIR"; mkdir -p "$BUILD_DIR"; cd "$BUILD_DIR"

"$SOURCE_DIR/configure" \
    --host="$TARGET" \
    --prefix=/usr \
    --disable-nls \
    CC="${CROSS}-gcc" \
    CFLAGS="--sysroot=$SYSROOT -I$SYSROOT/usr/include" \
    LDFLAGS="--sysroot=$SYSROOT -L$SYSROOT/usr/lib"

make -j"$(nproc)"
make DESTDIR="$SYSROOT" install

echo "[✓] diffutils $VERSION installed into sysroot."
