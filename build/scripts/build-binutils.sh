#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="binutils"
VERSION="$BINUTILS_VERSION"

ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.xz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BUILD_DIR="$KRATOS_WORK/$PACKAGE-build"

URL="https://ftp.gnu.org/gnu/binutils/binutils-$VERSION.tar.xz"

echo "================================"
echo "       KRATOSOS BINUTILS"
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
    echo "[+] Downloading Binutils..."
    curl -L "$URL" -o "$ARCHIVE"
else
    echo "[+] Binutils archive already exists."
fi

if [ ! -d "$SOURCE_DIR" ]; then
    echo "[+] Extracting Binutils..."
    tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"
else
    echo "[+] Binutils sources already extracted."
fi

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"

echo "[+] Configuring Binutils..."

"$SOURCE_DIR/configure" \
    --target="$TARGET" \
    --prefix="$KRATOS_TOOLS" \
    --with-sysroot="$KRATOS_SYSROOT" \
    --disable-nls \
    --disable-werror \
    --disable-gprofng
echo "[+] Building Binutils..."

make -j"$(nproc)"

echo "[+] Installing Binutils..."

make install

echo
echo "[+] Binutils installed successfully."
echo
echo "Toolchain:"
echo "  $KRATOS_TOOLS/bin/$TARGET-as"
echo "  $KRATOS_TOOLS/bin/$TARGET-ld"
echo "  $KRATOS_TOOLS/bin/$TARGET-objcopy"
