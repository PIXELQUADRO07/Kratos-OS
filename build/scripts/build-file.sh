#!/usr/bin/env bash
# build-file.sh — Cross-compile the 'file' command for KratosOS (Phase 2, step 14)
# Identifies file types via magic database.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="file"
VERSION="$FILE_VERSION"
ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.gz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BUILD_DIR="$KRATOS_WORK/$PACKAGE-build"
# Build a native (host) copy first so we have 'file' to generate the magic DB
NATIVE_BUILD_DIR="$KRATOS_WORK/$PACKAGE-native-build"
URL="https://astron.com/pub/file/file-$VERSION.tar.gz"

CROSS="$KRATOS_TOOLS/bin/$TARGET"
SYSROOT="$KRATOS_SYSROOT"

echo "========================================"
echo "         KRATOSOS FILE $VERSION"
echo "========================================"
mkdir -p "$KRATOS_DOWNLOADS" "$KRATOS_SOURCES" "$KRATOS_WORK"

if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading file $VERSION..."
    curl -L "$URL" -o "$ARCHIVE"
fi
if [ ! -d "$SOURCE_DIR" ]; then tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"; fi

# ── Step 1: Build native 'file' so we can run it during cross-build ──
echo "[+] Building native 'file' for the host..."
rm -rf "$NATIVE_BUILD_DIR"; mkdir -p "$NATIVE_BUILD_DIR"
cd "$NATIVE_BUILD_DIR"
"$SOURCE_DIR/configure" --prefix="$KRATOS_WORK/file-native-install" --disable-shared
make -j"$(nproc)"
make install
NATIVE_FILE="$KRATOS_WORK/file-native-install/bin/file"

# ── Step 2: Cross-compile ─────────────────────────────────────────────
echo "[+] Cross-compiling 'file'..."
rm -rf "$BUILD_DIR"; mkdir -p "$BUILD_DIR"; cd "$BUILD_DIR"

"$SOURCE_DIR/configure" \
    --host="$TARGET" \
    --prefix=/usr \
    --disable-bzlib \
    --disable-xzlib \
    --disable-zlib \
    CC="${CROSS}-gcc" \
    CFLAGS="--sysroot=$SYSROOT -I$SYSROOT/usr/include" \
    LDFLAGS="--sysroot=$SYSROOT -L$SYSROOT/usr/lib" \
    FILE="$NATIVE_FILE"

make -j"$(nproc)" FILE="$NATIVE_FILE"
make DESTDIR="$SYSROOT" install FILE="$NATIVE_FILE"

echo "[✓] file $VERSION installed into sysroot."
