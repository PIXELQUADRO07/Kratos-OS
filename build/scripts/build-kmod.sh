#!/usr/bin/env bash
# build-kmod.sh — Cross-compile kmod for KratosOS (Phase 2, step 11)
#
# Produces: /usr/bin/kmod and symlinks for modprobe, insmod, lsmod, rmmod, depmod.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="kmod"
VERSION="$KMOD_VERSION"
ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.xz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BUILD_DIR="$KRATOS_WORK/$PACKAGE-build"
URL="https://www.kernel.org/pub/linux/utils/kernel/kmod/kmod-$VERSION.tar.xz"

CROSS="$KRATOS_TOOLS/bin/$TARGET"
SYSROOT="$KRATOS_SYSROOT"

echo "========================================"
echo "         KRATOSOS KMOD $VERSION"
echo "========================================"
mkdir -p "$KRATOS_DOWNLOADS" "$KRATOS_SOURCES" "$KRATOS_WORK"

if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading kmod $VERSION..."
    curl -L "$URL" -o "$ARCHIVE"
fi

if [ ! -d "$SOURCE_DIR" ]; then
    echo "[+] Extracting kmod..."
    tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"
fi

rm -rf "$BUILD_DIR"; mkdir -p "$BUILD_DIR"; cd "$BUILD_DIR"

echo "[+] Configuring kmod..."
"$SOURCE_DIR/configure" \
    --host="$TARGET" \
    --prefix=/usr \
    --bindir=/usr/bin \
    --sysconfdir=/etc \
    --with-xz \
    --with-zstd \
    --with-zlib \
    CC="${CROSS}-gcc" \
    CFLAGS="--sysroot=$SYSROOT -I$SYSROOT/usr/include" \
    LDFLAGS="--sysroot=$SYSROOT -L$SYSROOT/usr/lib" \
    liblzma_CFLAGS="-I$SYSROOT/usr/include" \
    liblzma_LIBS="-L$SYSROOT/usr/lib -llzma" \
    zstd_CFLAGS="-I$SYSROOT/usr/include" \
    zstd_LIBS="-L$SYSROOT/usr/lib -lzstd" \
    zlib_CFLAGS="-I$SYSROOT/usr/include" \
    zlib_LIBS="-L$SYSROOT/usr/lib -lz" \
    PKG_CONFIG=/bin/false

echo "[+] Building kmod..."
make -j"$(nproc)"

echo "[+] Installing kmod..."
make DESTDIR="$SYSROOT" install

# Create traditional symlinks in /sbin (standard path for modprobe)
mkdir -p "$SYSROOT/sbin"
for tool in bin/kmod bin/lsmod bin/insmod bin/rmmod bin/modprobe bin/depmod bin/modinfo; do
    if [ -f "$SYSROOT/usr/$tool" ]; then
        ln -sfv "/usr/$tool" "$SYSROOT/sbin/$(basename $tool)"
    fi
done

echo "[✓] kmod $VERSION installed into sysroot."
