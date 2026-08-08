#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="glibc"
VERSION="$GLIBC_VERSION"

ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.xz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BUILD_DIR="$KRATOS_WORK/$PACKAGE-bootstrap-build"

URL="https://ftp.gnu.org/gnu/glibc/glibc-$VERSION.tar.xz"

echo "================================"
echo "     KRATOSOS GLIBC BOOTSTRAP"
echo "================================"
echo
echo "Target:  $TARGET"
echo "Version: $VERSION"
echo

mkdir -p "$KRATOS_DOWNLOADS"
mkdir -p "$KRATOS_SOURCES"
mkdir -p "$KRATOS_WORK"
mkdir -p "$KRATOS_SYSROOT/usr/lib"
mkdir -p "$KRATOS_SYSROOT/usr/include"

if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading glibc..."
    curl -L "$URL" -o "$ARCHIVE"
else
    echo "[+] glibc archive already exists."
fi

if [ ! -d "$SOURCE_DIR" ]; then
    echo "[+] Extracting glibc..."
    tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"
else
    echo "[+] glibc sources already extracted."
fi

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"

echo "[+] Configuring glibc (bootstrap)..."

BUILD_TRIPLET="$("$SOURCE_DIR/scripts/config.guess")"

CC="$KRATOS_TOOLS/bin/$TARGET-gcc" \
AR="$KRATOS_TOOLS/bin/$TARGET-ar" \
RANLIB="$KRATOS_TOOLS/bin/$TARGET-ranlib" \
"$SOURCE_DIR/configure" \
    --prefix=/usr \
    --host="$TARGET" \
    --build="$BUILD_TRIPLET" \
    --with-headers="$KRATOS_SYSROOT/usr/include" \
    --disable-nscd \
    --without-selinux \
    libc_cv_slibdir=/usr/lib

echo "[+] Building glibc startfiles (csu)..."

# We do NOT run "make" here: a full glibc build needs libgcc_s, which
# doesn't exist yet. csu/subdir_lib is enough to produce the crt*.o
# objects that libgcc itself needs to link its shared runtime.
make -j"$(nproc)" csu/subdir_lib

echo "[+] Installing bootstrap startfiles into sysroot..."

install -v -m644 csu/crt1.o csu/crti.o csu/crtn.o "$KRATOS_SYSROOT/usr/lib/"

# libgcc's build probes for -lc, so a stub is required at this stage.
# It will be overwritten by the real libc.so once "Glibc completa" runs.
"$KRATOS_TOOLS/bin/$TARGET-gcc" -nostdlib -nostartfiles -shared -x c /dev/null \
    -o "$KRATOS_SYSROOT/usr/lib/libc.so"

echo
echo "[+] glibc bootstrap complete."
echo
echo "Installed:"
echo "  $KRATOS_SYSROOT/usr/lib/crt1.o"
echo "  $KRATOS_SYSROOT/usr/lib/crti.o"
echo "  $KRATOS_SYSROOT/usr/lib/crtn.o"
echo "  $KRATOS_SYSROOT/usr/lib/libc.so (stub)"
echo
echo "Next step: build-libgcc.sh"
