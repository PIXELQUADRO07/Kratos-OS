#!/usr/bin/env bash
# build-readline.sh — Build and install readline into KratosOS sysroot (Phase 2, step 2)
#
# readline provides interactive line editing (history, completion) used by bash.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="readline"
VERSION="$READLINE_VERSION"
ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.gz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BUILD_DIR="$KRATOS_WORK/$PACKAGE-build"
URL="https://ftp.gnu.org/gnu/readline/readline-$VERSION.tar.gz"

CROSS="$KRATOS_TOOLS/bin/$TARGET"
SYSROOT="$KRATOS_SYSROOT"

echo "========================================"
echo "       KRATOSOS READLINE $VERSION"
echo "========================================"
echo "  Target:  $TARGET"
echo "  Sysroot: $SYSROOT"
echo

mkdir -p "$KRATOS_DOWNLOADS" "$KRATOS_SOURCES" "$KRATOS_WORK"

# ── Download ──────────────────────────────────
if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading readline $VERSION..."
    curl -L "$URL" -o "$ARCHIVE"
else
    echo "[~] readline already downloaded."
fi

# ── Extract ───────────────────────────────────
if [ ! -d "$SOURCE_DIR" ]; then
    echo "[+] Extracting readline..."
    tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"
else
    echo "[~] readline already extracted."
fi

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$SOURCE_DIR"

echo "[+] Configuring readline..."
# readline uses in-source build
./configure \
    --host="$TARGET" \
    --prefix=/usr \
    --disable-static \
    --with-curses \
    CC="${CROSS}-gcc" \
    AR="${CROSS}-ar" \
    RANLIB="${CROSS}-ranlib" \
    CFLAGS="--sysroot=$SYSROOT -I$SYSROOT/usr/include" \
    LDFLAGS="--sysroot=$SYSROOT -L$SYSROOT/usr/lib"

echo "[+] Building readline ($(nproc) jobs)..."
make -j"$(nproc)"

echo "[+] Installing readline into sysroot..."
make DESTDIR="$SYSROOT" install

cd "$KRATOS_ROOT"
echo "[✓] readline $VERSION installed into sysroot."
