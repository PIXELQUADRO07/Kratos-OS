#!/usr/bin/env bash
# build-tar.sh — Cross-compile GNU tar for KratosOS (Phase 2, step 10)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="tar"
VERSION="$TAR_VERSION"
ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.xz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BUILD_DIR="$KRATOS_WORK/$PACKAGE-build"
URL="https://ftp.gnu.org/gnu/tar/tar-$VERSION.tar.xz"

CROSS="$KRATOS_TOOLS/bin/$TARGET"
SYSROOT="$KRATOS_SYSROOT"

echo "========================================"
echo "         KRATOSOS TAR $VERSION"
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
    LDFLAGS="--sysroot=$SYSROOT -L$SYSROOT/usr/lib" \
    FORCE_UNSAFE_CONFIGURE=1

make -j"$(nproc)"
make DESTDIR="$SYSROOT" install

echo "[✓] tar $VERSION installed into sysroot."
