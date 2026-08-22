#!/usr/bin/env bash
# build-zlib.sh — Cross-compile zlib for KratosOS
#
# Produces: /usr/lib/libz.so and headers.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

# If ZLIB_VERSION is not in versions.conf, default to 1.3.1
PACKAGE="zlib"
VERSION="${ZLIB_VERSION:-1.3.1}"
ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.gz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BUILD_DIR="$KRATOS_WORK/$PACKAGE-build"
URL="https://zlib.net/zlib-$VERSION.tar.gz"

CROSS="$KRATOS_TOOLS/bin/$TARGET"
SYSROOT="$KRATOS_SYSROOT"

echo "========================================"
echo "         KRATOSOS ZLIB $VERSION"
echo "========================================"
mkdir -p "$KRATOS_DOWNLOADS" "$KRATOS_SOURCES" "$KRATOS_WORK"

if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading zlib $VERSION..."
    curl -L "$URL" -o "$ARCHIVE"
fi

if [ ! -d "$SOURCE_DIR" ]; then
    echo "[+] Extracting zlib..."
    tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"
fi

rm -rf "$BUILD_DIR"; mkdir -p "$BUILD_DIR"; cd "$BUILD_DIR"

# zlib's configure is NOT a standard autoconf script.
# It doesn't support --host, we must use environment variables.
echo "[+] Configuring zlib..."
cp -r "$SOURCE_DIR/." .

CHOST="$TARGET" \
CC="${CROSS}-gcc" \
AR="${CROSS}-ar" \
RANLIB="${CROSS}-ranlib" \
./configure \
    --prefix=/usr \
    --shared

echo "[+] Building zlib..."
make -j"$(nproc)"

echo "[+] Installing zlib..."
make DESTDIR="$SYSROOT" install

echo "[✓] zlib $VERSION installed into sysroot."
