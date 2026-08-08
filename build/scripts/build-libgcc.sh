#!/usr/bin/env bash
# build-libgcc.sh — Build target libgcc in a dedicated build directory.
#
# Configures and builds target-libgcc independently without re-triggering
# host GCC tool compilation.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="gcc"
VERSION="$GCC_VERSION"

SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BUILD_DIR="$KRATOS_WORK/$PACKAGE-libgcc-build"

echo "========================================"
echo "        KRATOSOS LIBGCC"
echo "========================================"
echo
echo "  Target:    $TARGET"
echo "  Version:   $VERSION"
echo "  Build dir: $BUILD_DIR"
echo "  Sysroot:   $KRATOS_SYSROOT"
echo

if [ ! -d "$SOURCE_DIR" ]; then
    echo "[!] GCC sources not found: $SOURCE_DIR"
    echo "    Run build-gcc-pass1.sh first."
    exit 1
fi

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "[+] Configuring libgcc in dedicated build directory..."

"$SOURCE_DIR/configure"         \
    --target="$TARGET"          \
    --prefix="$KRATOS_TOOLS"    \
    --with-sysroot="$KRATOS_SYSROOT" \
    --enable-languages=c        \
    --enable-shared             \
    --disable-nls               \
    --disable-multilib          \
    --disable-threads           \
    --disable-libatomic         \
    --disable-libgomp           \
    --disable-libquadmath       \
    --disable-libssp            \
    --disable-libvtv            \
    --disable-libstdcxx         \
    --disable-libsanitizer      \
    --disable-libcody

echo "[+] Building target libgcc..."
make -j"$(nproc)" all-target-libgcc

echo "[+] Installing target libgcc..."
make install-target-libgcc

LIBGCC_DIR="$KRATOS_TOOLS/$TARGET/lib"

if ls "$LIBGCC_DIR/libgcc_s.so"* &>/dev/null; then
    echo "  [✓] libgcc_s present in $LIBGCC_DIR"
    mkdir -p "$KRATOS_SYSROOT/usr/lib"
    cp -av "$LIBGCC_DIR/libgcc_s.so"* "$KRATOS_SYSROOT/usr/lib/" 2>/dev/null || true
else
    echo "  [~] Note: libgcc_s.so not built in this stage (will be built in GCC pass 2)."
fi

echo
echo "[✓] libgcc stage complete."
