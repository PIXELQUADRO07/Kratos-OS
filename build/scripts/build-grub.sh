#!/usr/bin/env bash

# build-grub.sh — Build GRUB 2 for KratosOS

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="grub"
VERSION="$GRUB_VERSION"

ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.xz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BUILD_DIR="$KRATOS_WORK/$PACKAGE-build"

SYSROOT="$KRATOS_SYSROOT"
TOOLS="$KRATOS_TOOLS"

TARGET="$TARGET"

echo "========================================"
echo "       KRATOSOS GRUB $VERSION"
echo "========================================"
echo "  Target:  $TARGET"
echo "  Sysroot: $SYSROOT"
echo

# ── Incremental guard ─────────────────────────────────────────────────
# Skip the build entirely if the GRUB EFI modules are already installed
# in the sysroot. The normal.mod file is the canonical indicator that
# grub-install will work correctly; if it is present we assume the full
# GRUB installation is intact.
GRUB_MARKER="$SYSROOT/usr/lib/grub/x86_64-efi/normal.mod"
if [ -f "$GRUB_MARKER" ]; then
    echo "[✓] GRUB $VERSION already installed (found $GRUB_MARKER)."
    echo "[~] Skipping build. Run with --force or remove the sysroot to rebuild."
    exit 0
fi

mkdir -p "$KRATOS_DOWNLOADS"
mkdir -p "$KRATOS_SOURCES"
mkdir -p "$KRATOS_WORK"
mkdir -p "$SYSROOT/boot/grub"
mkdir -p "$SYSROOT/boot/efi/EFI/KratosOS"

# ------------------------------------------------------------
# Download
# ------------------------------------------------------------

if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading GRUB $VERSION..."

    curl -L \
        "https://ftp.gnu.org/gnu/grub/grub-$VERSION.tar.xz" \
        -o "$ARCHIVE"
else
    echo "[~] GRUB archive already present."
fi

# ------------------------------------------------------------
# Extract
# ------------------------------------------------------------

if [ ! -d "$SOURCE_DIR" ]; then
    echo "[+] Extracting GRUB..."
    tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"
else
    echo "[~] GRUB source already extracted."
fi

# ------------------------------------------------------------
# Clean build directory
# ------------------------------------------------------------

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"

# ------------------------------------------------------------
# Cross compiler
# ------------------------------------------------------------

CROSS="$TOOLS/bin/$TARGET"

export PATH="$TOOLS/bin:$PATH"

export CC="${CROSS}-gcc"
export AR="${CROSS}-ar"
export AS="${CROSS}-as"
export LD="${CROSS}-ld"
export NM="${CROSS}-nm"
export OBJCOPY="${CROSS}-objcopy"
export RANLIB="${CROSS}-ranlib"
export STRIP="${CROSS}-strip"

export CFLAGS="--sysroot=$SYSROOT"
export CPPFLAGS="--sysroot=$SYSROOT -I$SYSROOT/usr/include"
export LDFLAGS="--sysroot=$SYSROOT -L$SYSROOT/usr/lib"

# Do not use host packages/libraries during target configuration
export PKG_CONFIG=/bin/false
export PKG_CONFIG_PATH=
export PKG_CONFIG_LIBDIR=
# ------------------------------------------------------------
# Configure
# ------------------------------------------------------------

echo "[+] Configuring GRUB..."

"$SOURCE_DIR/configure" \
    --host="$TARGET" \
    --build="$(gcc -dumpmachine)" \
    --prefix=/usr \
    --with-platform=efi \
    --target=x86_64 \
    --disable-werror
# ------------------------------------------------------------
# Build
# ------------------------------------------------------------

echo "[+] Building GRUB ($(nproc) jobs)..."

make -j"$(nproc)"

# ------------------------------------------------------------
# Install into sysroot
# ------------------------------------------------------------

echo "[+] Installing GRUB into sysroot..."

make DESTDIR="$SYSROOT" install

echo
echo "[✓] GRUB $VERSION installed successfully."
echo
echo "GRUB files:"
find "$SYSROOT/usr/lib/grub" -maxdepth 2 -type d 2>/dev/null || true
