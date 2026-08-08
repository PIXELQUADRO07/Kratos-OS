#!/usr/bin/env bash
# build-bzip2.sh — Cross-compile bzip2 for KratosOS (Phase 2, step 13)
#
# bzip2 uses a hand-written Makefile, not autoconf, so we handle it manually.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="bzip2"
VERSION="$BZIP2_VERSION"
ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.gz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
URL="https://sourceware.org/pub/bzip2/bzip2-$VERSION.tar.gz"

CROSS="$KRATOS_TOOLS/bin/$TARGET"
SYSROOT="$KRATOS_SYSROOT"

echo "========================================"
echo "        KRATOSOS BZIP2 $VERSION"
echo "========================================"
mkdir -p "$KRATOS_DOWNLOADS" "$KRATOS_SOURCES"

if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading bzip2 $VERSION..."
    curl -L "$URL" -o "$ARCHIVE"
fi
if [ ! -d "$SOURCE_DIR" ]; then tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"; fi

cd "$SOURCE_DIR"

echo "[+] Building bzip2 shared library..."
make -f Makefile-libbz2_so \
    CC="${CROSS}-gcc" \
    AR="${CROSS}-ar" \
    RANLIB="${CROSS}-ranlib" \
    CFLAGS="-Wall -Winline -O2 --sysroot=$SYSROOT -fPIC"

echo "[+] Building bzip2 static..."
make -j"$(nproc)" \
    CC="${CROSS}-gcc" \
    AR="${CROSS}-ar" \
    RANLIB="${CROSS}-ranlib" \
    CFLAGS="-Wall -Winline -O2 --sysroot=$SYSROOT"

echo "[+] Installing bzip2 into sysroot..."
make install PREFIX="$SYSROOT/usr" \
    CC="${CROSS}-gcc" \
    AR="${CROSS}-ar" \
    RANLIB="${CROSS}-ranlib"

# Install shared library
install -vm755 libbz2.so.$VERSION "$SYSROOT/usr/lib/"
ln -sfv libbz2.so.$VERSION "$SYSROOT/usr/lib/libbz2.so.1.0"
ln -sfv libbz2.so.$VERSION "$SYSROOT/usr/lib/libbz2.so"

# Install bzip2 linked against the shared lib
${CROSS}-gcc \
    --sysroot="$SYSROOT" \
    -L"$SYSROOT/usr/lib" \
    -o "$SYSROOT/usr/bin/bzip2" \
    bzip2.c -lbz2

ln -sfv bzip2 "$SYSROOT/usr/bin/bzcat"
ln -sfv bzip2 "$SYSROOT/usr/bin/bunzip2"

echo "[✓] bzip2 $VERSION installed into sysroot."
