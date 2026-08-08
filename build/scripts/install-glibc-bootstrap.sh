#!/usr/bin/env bash

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

source "$PROJECT_ROOT/build/config/versions.conf"

TARGET="x86_64-kratos-linux-gnu"
KRATOS_TOOLS="$PROJECT_ROOT/build/tools"
KRATOS_SYSROOT="$PROJECT_ROOT/build/sysroot"

export PATH="$KRATOS_TOOLS/bin:$PATH"

SOURCE_DIR="$PROJECT_ROOT/build/sources/glibc-$GLIBC_VERSION"
BUILD_DIR="$PROJECT_ROOT/build/work/glibc-bootstrap-build"

echo "================================"
echo "   KRATOSOS GLIBC BOOTSTRAP"
echo "================================"
echo
echo "glibc version : $GLIBC_VERSION"
echo "Target        : $TARGET"
echo "Sysroot       : $KRATOS_SYSROOT"
echo

if [ ! -d "$SOURCE_DIR" ]; then
    echo "[!] Glibc source directory not found:"
    echo "    $SOURCE_DIR"
    echo
    echo "Run build-glibc.sh/download step first."
    exit 1
fi

mkdir -p "$BUILD_DIR"
mkdir -p "$KRATOS_SYSROOT"

rm -rf "$BUILD_DIR"/*
cd "$BUILD_DIR"

echo "[+] Configuring glibc for bootstrap..."

"$SOURCE_DIR/configure" \
    --prefix=/usr \
    --build="$(/usr/bin/gcc -dumpmachine)" \
    --host="$TARGET" \
    --with-headers="$KRATOS_SYSROOT/usr/include" \
    --disable-nscd \
    --disable-werror \
    libc_cv_forced_unwind=yes \
    libc_cv_c_cleanup=yes

echo
echo "[+] Installing bootstrap headers..."

make install-bootstrap-headers=yes \
     install-headers \
     DESTDIR="$KRATOS_SYSROOT"

echo
echo "[+] Building libc startup objects..."

make -C csu \
    libc-start.o \
    DESTDIR="$KRATOS_SYSROOT"

echo
echo "[+] Installing startup objects..."

install -m 644 csu/crt1.o \
    "$KRATOS_SYSROOT/usr/lib/"

install -m 644 csu/crti.o \
    "$KRATOS_SYSROOT/usr/lib/"

install -m 644 csu/crtn.o \
    "$KRATOS_SYSROOT/usr/lib/"

echo
echo "[+] Creating temporary libc archive..."

touch "$KRATOS_SYSROOT/usr/lib/libc.a"

echo
echo "[+] Glibc bootstrap stage completed successfully."
echo
echo "Headers:"
echo "  $KRATOS_SYSROOT/usr/include"
echo
echo "Startup objects:"
echo "  $KRATOS_SYSROOT/usr/lib/crt1.o"
echo "  $KRATOS_SYSROOT/usr/lib/crti.o"
echo "  $KRATOS_SYSROOT/usr/lib/crtn.o"
echo
echo "Temporary libc:"
echo "  $KRATOS_SYSROOT/usr/lib/libc.a"
