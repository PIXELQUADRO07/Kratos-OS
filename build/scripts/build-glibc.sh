#!/usr/bin/env bash

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

source "$PROJECT_ROOT/build/config/versions.conf"

TARGET="x86_64-kratos-linux-gnu"
KRATOS_TOOLS="$PROJECT_ROOT/build/tools"
KRATOS_SYSROOT="$PROJECT_ROOT/build/sysroot"

export PATH="$KRATOS_TOOLS/bin:$PATH"

SOURCE_DIR="$PROJECT_ROOT/build/sources/glibc-$GLIBC_VERSION"
BUILD_DIR="$PROJECT_ROOT/build/work/glibc-build"

URL="https://ftp.gnu.org/gnu/glibc/glibc-$GLIBC_VERSION.tar.xz"
ARCHIVE="$PROJECT_ROOT/build/downloads/glibc-$GLIBC_VERSION.tar.xz"

echo "================================"
echo "       KRATOSOS GLIBC"
echo "================================"
echo
echo "glibc version : $GLIBC_VERSION"
echo "Target        : $TARGET"
echo "Sysroot       : $KRATOS_SYSROOT"
echo

mkdir -p "$PROJECT_ROOT/build/downloads"
mkdir -p "$PROJECT_ROOT/build/sources"
mkdir -p "$PROJECT_ROOT/build/work"
mkdir -p "$KRATOS_SYSROOT"

if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading glibc $GLIBC_VERSION..."
    curl -L "$URL" -o "$ARCHIVE"
else
    echo "[+] glibc archive already downloaded."
fi

if [ ! -d "$SOURCE_DIR" ]; then
    echo "[+] Extracting glibc..."
    tar -xf "$ARCHIVE" -C "$PROJECT_ROOT/build/sources"
else
    echo "[+] glibc source already extracted."
fi

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"

echo "[+] Configuring glibc..."

"$SOURCE_DIR/configure" \
    --prefix=/usr \
    --build="$(/usr/bin/gcc -dumpmachine)" \
    --host="$TARGET" \
    --with-headers="$KRATOS_SYSROOT/usr/include" \
    --disable-werror \
    libc_cv_forced_unwind=yes \
    libc_cv_c_cleanup=yes

echo "[+] Building glibc..."

make -j"$(nproc)"

echo "[+] Installing glibc into KratosOS sysroot..."

make DESTDIR="$KRATOS_SYSROOT" install

echo
echo "[+] glibc installed successfully."
echo
echo "Sysroot:"
echo "  $KRATOS_SYSROOT"
