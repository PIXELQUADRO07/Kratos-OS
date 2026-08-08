#!/usr/bin/env bash
# build-coreutils.sh — Cross-compile coreutils for KratosOS (Phase 2, step 4)
#
# Provides: ls, cp, mv, rm, cat, echo, mkdir, chmod, chown, date, df, du, ...

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="coreutils"
VERSION="$COREUTILS_VERSION"
ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.xz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BUILD_DIR="$KRATOS_WORK/$PACKAGE-build"
URL="https://ftp.gnu.org/gnu/coreutils/coreutils-$VERSION.tar.xz"

CROSS="$KRATOS_TOOLS/bin/$TARGET"
SYSROOT="$KRATOS_SYSROOT"

echo "========================================"
echo "      KRATOSOS COREUTILS $VERSION"
echo "========================================"
echo "  Target:  $TARGET"
echo "  Sysroot: $SYSROOT"
echo

mkdir -p "$KRATOS_DOWNLOADS" "$KRATOS_SOURCES" "$KRATOS_WORK"

# ── Download ──────────────────────────────────
if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading coreutils $VERSION..."
    curl -L "$URL" -o "$ARCHIVE"
else
    echo "[~] coreutils already downloaded."
fi

# ── Extract ───────────────────────────────────
if [ ! -d "$SOURCE_DIR" ]; then
    echo "[+] Extracting coreutils..."
    tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"
else
    echo "[~] coreutils already extracted."
fi

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "[+] Configuring coreutils..."
"$SOURCE_DIR/configure" \
    --host="$TARGET" \
    --prefix=/usr \
    --enable-no-install-program=kill,uptime \
    --disable-nls \
    CC="${CROSS}-gcc" \
    AR="${CROSS}-ar" \
    RANLIB="${CROSS}-ranlib" \
    CFLAGS="--sysroot=$SYSROOT -I$SYSROOT/usr/include" \
    LDFLAGS="--sysroot=$SYSROOT -L$SYSROOT/usr/lib" \
    fu_cv_sys_stat_statfs2_bsize=yes \
    gl_cv_func_working_mkstemp=yes

echo "[+] Building coreutils ($(nproc) jobs)..."
make -j"$(nproc)"

echo "[+] Installing coreutils into sysroot..."
make DESTDIR="$SYSROOT" install

echo "[✓] coreutils $VERSION installed into sysroot."
