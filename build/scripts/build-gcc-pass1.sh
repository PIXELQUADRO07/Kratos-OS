#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="gcc"
VERSION="$GCC_VERSION"

ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.xz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BUILD_DIR="$KRATOS_WORK/$PACKAGE-pass1-build"

URL="https://ftp.gnu.org/gnu/gcc/gcc-$VERSION/gcc-$VERSION.tar.xz"

echo "================================"
echo "       KRATOSOS GCC PASS 1"
echo "================================"
echo
echo "Target:  $TARGET"
echo "Version: $VERSION"
echo

mkdir -p "$KRATOS_DOWNLOADS"
mkdir -p "$KRATOS_SOURCES"
mkdir -p "$KRATOS_WORK"
mkdir -p "$KRATOS_TOOLS"

if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading GCC..."
    curl -L "$URL" -o "$ARCHIVE"
else
    echo "[+] GCC archive already exists."
fi

if [ ! -d "$SOURCE_DIR" ]; then
    echo "[+] Extracting GCC..."
    tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"
else
    echo "[+] GCC sources already extracted."
fi

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"

echo "[+] Configuring GCC pass 1..."

"$SOURCE_DIR/configure" \
    --target="$TARGET" \
    --prefix="$KRATOS_TOOLS" \
    --with-sysroot="$KRATOS_SYSROOT" \
    --disable-nls \
    --disable-shared \
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
    --enable-languages=c
echo "[+] Building GCC pass 1..."

make all-gcc -j"$(nproc)"

echo "[+] Installing GCC pass 1..."

make install-gcc

echo
echo "[+] GCC pass 1 installed successfully."
echo
echo "Compiler:"
echo "  $KRATOS_TOOLS/bin/$TARGET-gcc"
