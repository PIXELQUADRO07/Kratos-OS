#!/usr/bin/env bash
# build-zstd.sh — Cross-compile zstd for KratosOS
#
# Produces: /usr/lib/libzstd.so and /usr/bin/zstd.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="zstd"
VERSION="$ZSTD_VERSION"
ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.gz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
URL="https://github.com/facebook/zstd/releases/download/v$VERSION/zstd-$VERSION.tar.gz"

CROSS="$KRATOS_TOOLS/bin/$TARGET"
SYSROOT="$KRATOS_SYSROOT"

echo "========================================"
echo "         KRATOSOS ZSTD $VERSION"
echo "========================================"
mkdir -p "$KRATOS_DOWNLOADS" "$KRATOS_SOURCES"

if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading zstd $VERSION..."
    curl -L "$URL" -o "$ARCHIVE"
fi

if [ ! -d "$SOURCE_DIR" ]; then
    echo "[+] Extracting zstd..."
    tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"
fi

cd "$SOURCE_DIR"

echo "[+] Building zstd..."
# zstd uses a plain Makefile. We need to pass cross-compiler and prefix.
make -j"$(nproc)" \
    CC="${CROSS}-gcc" \
    AR="${CROSS}-ar" \
    RANLIB="${CROSS}-ranlib" \
    PREFIX=/usr

echo "[+] Installing zstd..."
make DESTDIR="$SYSROOT" PREFIX=/usr install

echo "[✓] zstd $VERSION installed into sysroot."
