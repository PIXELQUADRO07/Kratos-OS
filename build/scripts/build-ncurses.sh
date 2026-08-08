#!/usr/bin/env bash
# build-ncurses.sh — Build and install ncurses into KratosOS sysroot (Phase 2, step 1)
#
# ncurses is a terminal-handling library required by bash (readline), vim, etc.
# We install it into KRATOS_SYSROOT/usr so the cross-compiler can find it.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="ncurses"
VERSION="$NCURSES_VERSION"
ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.gz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BUILD_DIR="$KRATOS_WORK/$PACKAGE-build"
URL="https://ftp.gnu.org/gnu/ncurses/ncurses-$VERSION.tar.gz"

CROSS="$KRATOS_TOOLS/bin/$TARGET"
SYSROOT="$KRATOS_SYSROOT"

echo "========================================"
echo "       KRATOSOS NCURSES $VERSION"
echo "========================================"
echo "  Target:  $TARGET"
echo "  Sysroot: $SYSROOT"
echo

mkdir -p "$KRATOS_DOWNLOADS" "$KRATOS_SOURCES" "$KRATOS_WORK"

# ── Download ──────────────────────────────────
if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading ncurses $VERSION..."
    curl -L "$URL" -o "$ARCHIVE"
else
    echo "[~] ncurses already downloaded."
fi

# ── Extract ───────────────────────────────────
if [ ! -d "$SOURCE_DIR" ]; then
    echo "[+] Extracting ncurses..."
    tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"
else
    echo "[~] ncurses already extracted."
fi

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "[+] Configuring ncurses..."
"$SOURCE_DIR/configure" \
    --host="$TARGET" \
    --prefix=/usr \
    --with-sysroot="$SYSROOT" \
    --with-shared \
    --without-debug \
    --without-ada \
    --enable-widec \
    --without-manpages \
    --without-tests \
    CC="${CROSS}-gcc" \
    CXX="${CROSS}-g++" \
    AR="${CROSS}-ar" \
    RANLIB="${CROSS}-ranlib" \
    STRIP="${CROSS}-strip"

echo "[+] Building ncurses ($(nproc) jobs)..."
make -j"$(nproc)"

echo "[+] Installing ncurses into sysroot..."
make DESTDIR="$SYSROOT" install

# Create non-wide compatibility symlinks (many programs link -lncurses)
for lib in ncurses form panel menu; do
    ln -sfv "lib${lib}w.so" "$SYSROOT/usr/lib/lib${lib}.so"    2>/dev/null || true
    ln -sfv "lib${lib}w.a"  "$SYSROOT/usr/lib/lib${lib}.a"     2>/dev/null || true
done
# ncurses → curses alias
ln -sfv libncursesw.so "$SYSROOT/usr/lib/libcurses.so"         2>/dev/null || true

echo "[✓] ncurses $VERSION installed into sysroot."
