#!/usr/bin/env bash
# build-bash.sh — Cross-compile bash for KratosOS (Phase 2, step 3)
#
# bash depends on readline and ncurses, which must be installed into the sysroot first.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="bash"
VERSION="$BASH_VERSION"
ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.gz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BUILD_DIR="$KRATOS_WORK/$PACKAGE-build"
URL="https://ftp.gnu.org/gnu/bash/bash-$VERSION.tar.gz"

CROSS="$KRATOS_TOOLS/bin/$TARGET"
SYSROOT="$KRATOS_SYSROOT"

echo "========================================"
echo "         KRATOSOS BASH $VERSION"
echo "========================================"
echo "  Target:  $TARGET"
echo "  Sysroot: $SYSROOT"
echo

mkdir -p "$KRATOS_DOWNLOADS" "$KRATOS_SOURCES" "$KRATOS_WORK"

# ── Prerequisites ─────────────────────────────
for lib in "$SYSROOT/usr/lib/libncursesw.so" "$SYSROOT/usr/lib/libreadline.so"; do
    if [ ! -e "$lib" ]; then
        echo "[!] Missing sysroot library: $lib"
        echo "    Run build-ncurses.sh and build-readline.sh first."
        exit 1
    fi
done

# ── Download ──────────────────────────────────
if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading bash $VERSION..."
    curl -L "$URL" -o "$ARCHIVE"
else
    echo "[~] bash already downloaded."
fi

# ── Extract ───────────────────────────────────
if [ ! -d "$SOURCE_DIR" ]; then
    echo "[+] Extracting bash..."
    tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"
else
    echo "[~] bash already extracted."
fi

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "[+] Configuring bash..."
"$SOURCE_DIR/configure" \
    --host="$TARGET" \
    --prefix=/usr \
    --bindir=/bin \
    --without-bash-malloc \
    --with-installed-readline \
    --enable-readline \
    --disable-nls \
    CC="${CROSS}-gcc" \
    AR="${CROSS}-ar" \
    RANLIB="${CROSS}-ranlib" \
    CFLAGS="--sysroot=$SYSROOT -I$SYSROOT/usr/include" \
    LDFLAGS="--sysroot=$SYSROOT -L$SYSROOT/usr/lib" \
    bash_cv_dev_stdin=present \
    bash_cv_dev_fd=standard \
    bash_cv_dev_fd_list=standard \
    bash_cv_ulimit_maxfds=yes \
    bash_cv_func_sigsetjmp=present \
    bash_cv_func_ctype_nonascii=yes \
    bash_cv_wcontinued_broken=no \
    bash_cv_job_control_missing=present

echo "[+] Building bash ($(nproc) jobs)..."
make -j"$(nproc)"

echo "[+] Installing bash into sysroot..."
make DESTDIR="$SYSROOT" install

# Create /bin/sh → bash symlink
ln -sfv bash "$SYSROOT/bin/sh"

echo "[✓] bash $VERSION installed into sysroot."
echo "    /bin/bash → $(readlink -f "$SYSROOT/bin/bash" 2>/dev/null || echo 'installed')"
echo "    /bin/sh   → bash"
