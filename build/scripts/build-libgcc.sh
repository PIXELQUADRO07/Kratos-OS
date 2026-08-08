#!/usr/bin/env bash
# build-libgcc.sh — Build libgcc_s.so (shared GCC runtime library).
#
# Strategy: instead of re-configuring a full GCC source tree (error-prone),
# we re-enter the existing GCC pass-1 build directory and build just the
# target libgcc with shared libs enabled.
#
# This is the standard LFS/CLFS trick: configure once for the target, then
# selectively build sub-targets as needed.
#
# Prerequisites:
#   1. build-gcc-pass1.sh    (GCC pass 1 build dir must exist)
#   2. build-glibc-bootstrap.sh  (crt*.o + stub libc.so in sysroot)
#
# Output:
#   $KRATOS_TOOLS/$TARGET/lib/libgcc_s.so.1
#   $KRATOS_TOOLS/$TARGET/lib/libgcc_s.so   (symlink)
#   $KRATOS_TOOLS/lib/gcc/$TARGET/$GCC_VERSION/libgcc.a

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="gcc"
VERSION="$GCC_VERSION"

SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
# Reuse the pass-1 build directory — it is already configured for the target.
BUILD_DIR="$KRATOS_WORK/$PACKAGE-pass1-build"

echo "========================================"
echo "        KRATOSOS LIBGCC"
echo "========================================"
echo
echo "  Target:    $TARGET"
echo "  Version:   $VERSION"
echo "  Build dir: $BUILD_DIR"
echo "  Sysroot:   $KRATOS_SYSROOT"
echo

# ---------------------------------------------------------------------------
# Prerequisite checks
# ---------------------------------------------------------------------------

echo "[~] Checking prerequisites..."

if [ ! -d "$BUILD_DIR" ]; then
    echo "[!] GCC pass-1 build dir not found: $BUILD_DIR"
    echo "    Run build-gcc-pass1.sh first."
    exit 1
fi

if [ ! -f "$KRATOS_TOOLS/bin/$TARGET-gcc" ]; then
    echo "[!] Pass-1 compiler not found: $KRATOS_TOOLS/bin/$TARGET-gcc"
    echo "    Run build-gcc-pass1.sh first."
    exit 1
fi

for f in crt1.o crti.o crtn.o libc.so; do
    if [ ! -f "$KRATOS_SYSROOT/usr/lib/$f" ]; then
        echo "[!] Missing sysroot file: $KRATOS_SYSROOT/usr/lib/$f"
        echo "    Run build-glibc-bootstrap.sh first."
        exit 1
    fi
done

echo "  [✓] All prerequisites satisfied."
echo

# ---------------------------------------------------------------------------
# Re-configure the pass-1 build dir with --enable-shared
#
# We must reconfigure because pass 1 was built with --disable-shared.
# We run configure AGAIN in the same dir with shared enabled. GCC's
# configure is idempotent when the source tree and target don't change.
# ---------------------------------------------------------------------------

cd "$BUILD_DIR"

echo "[+] Re-configuring GCC pass-1 build dir with --enable-shared..."

"$SOURCE_DIR/configure"     \
    --target="$TARGET"      \
    --prefix="$KRATOS_TOOLS" \
    --with-sysroot="$KRATOS_SYSROOT" \
    --enable-languages=c    \
    --enable-shared         \
    --disable-nls           \
    --disable-multilib      \
    --disable-threads       \
    --disable-libatomic     \
    --disable-libgomp       \
    --disable-libquadmath   \
    --disable-libssp        \
    --disable-libvtv        \
    --disable-libstdcxx     \
    --disable-libsanitizer  \
    --disable-libcody

echo "[+] Building libgcc (shared)..."
make -j"$(nproc)" all-target-libgcc

echo "[+] Installing libgcc..."
make install-target-libgcc

# ---------------------------------------------------------------------------
# Verify
# ---------------------------------------------------------------------------

echo
echo "[~] Verifying libgcc installation..."

LIBGCC_DIR="$KRATOS_TOOLS/$TARGET/lib"

# Locate libgcc_s.so.1 (the actual shared lib)
if ls "$LIBGCC_DIR/libgcc_s.so"* &>/dev/null; then
    echo "  [✓] libgcc_s present:"
    ls -la "$LIBGCC_DIR/libgcc_s.so"* | sed 's/^/      /'
else
    echo "[!] libgcc_s.so NOT found in $LIBGCC_DIR"
    echo "    Files in that dir:"
    ls "$LIBGCC_DIR/" 2>/dev/null | sed 's/^/      /' || true
    exit 1
fi

# Also create a libgcc_s.so symlink if only libgcc_s.so.1 was installed
if [ -f "$LIBGCC_DIR/libgcc_s.so.1" ] && [ ! -f "$LIBGCC_DIR/libgcc_s.so" ]; then
    echo "  [+] Creating libgcc_s.so symlink..."
    ln -sfv libgcc_s.so.1 "$LIBGCC_DIR/libgcc_s.so"
fi

# Copy libgcc_s.so into the sysroot so glibc can link against it
echo "  [+] Copying libgcc_s.so into sysroot..."
mkdir -p "$KRATOS_SYSROOT/usr/lib"
cp -av "$LIBGCC_DIR/libgcc_s.so"* "$KRATOS_SYSROOT/usr/lib/" | sed 's/^/      /'
# The sysroot needs a plain libgcc_s.so linker script too
if [ ! -f "$KRATOS_SYSROOT/usr/lib/libgcc_s.so" ]; then
    ln -sfv libgcc_s.so.1 "$KRATOS_SYSROOT/usr/lib/libgcc_s.so"
fi

echo
echo "[✓] libgcc installed successfully."
echo
echo "  $LIBGCC_DIR/libgcc_s.so.1"
echo "  $KRATOS_SYSROOT/usr/lib/libgcc_s.so (sysroot copy)"
echo
echo "Next step: build-glibc.sh"
