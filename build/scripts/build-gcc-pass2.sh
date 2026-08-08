#!/usr/bin/env bash
# build-gcc-pass2.sh — Build the final KratosOS cross-compiler (GCC pass 2).
#
# This pass produces a full GCC with C and C++ support, linked against the
# real glibc installed in the sysroot during build-glibc.sh.
#
# Prerequisites (must run in order):
#   1. install-linux-headers.sh
#   2. build-binutils.sh
#   3. build-gcc-pass1.sh
#   4. build-glibc-bootstrap.sh
#   5. build-libgcc.sh
#   6. build-glibc.sh        ← must be complete before this pass
#
# Output:
#   $KRATOS_TOOLS/bin/x86_64-kratos-linux-gnu-gcc   (C compiler)
#   $KRATOS_TOOLS/bin/x86_64-kratos-linux-gnu-g++   (C++ compiler)
#   $KRATOS_TOOLS/lib/libstdc++.so.*

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="gcc"
VERSION="$GCC_VERSION"

ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.xz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BUILD_DIR="$KRATOS_WORK/$PACKAGE-pass2-build"

URL="https://ftp.gnu.org/gnu/gcc/gcc-$VERSION/gcc-$VERSION.tar.xz"

echo "========================================"
echo "        KRATOSOS GCC PASS 2"
echo "========================================"
echo
echo "  Version:  $VERSION"
echo "  Target:   $TARGET"
echo "  Prefix:   $KRATOS_TOOLS"
echo "  Sysroot:  $KRATOS_SYSROOT"
echo

# ---------------------------------------------------------------------------
# Prerequisite checks
# ---------------------------------------------------------------------------

echo "[~] Checking prerequisites..."

# GCC pass 1 compiler must exist
if [ ! -f "$KRATOS_TOOLS/bin/$TARGET-gcc" ]; then
    echo "[!] Pass 1 compiler not found: $KRATOS_TOOLS/bin/$TARGET-gcc"
    echo "    Run build-gcc-pass1.sh first."
    exit 1
fi

# glibc must be installed in sysroot
for lib in libc.so libc.a; do
    if [ ! -f "$KRATOS_SYSROOT/usr/lib/$lib" ]; then
        echo "[!] Missing sysroot library: $KRATOS_SYSROOT/usr/lib/$lib"
        echo "    Run build-glibc.sh first."
        exit 1
    fi
done


echo "  [✓] All prerequisites satisfied."
echo

# ---------------------------------------------------------------------------
# Download
# ---------------------------------------------------------------------------

mkdir -p "$KRATOS_DOWNLOADS" "$KRATOS_SOURCES" "$KRATOS_WORK" "$KRATOS_TOOLS"

if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading GCC $VERSION..."
    curl -L --progress-bar --retry 3 "$URL" -o "$ARCHIVE.part"
    mv "$ARCHIVE.part" "$ARCHIVE"
else
    echo "[=] Archive already present: $ARCHIVE"
fi

# ---------------------------------------------------------------------------
# Extract
# ---------------------------------------------------------------------------

if [ ! -d "$SOURCE_DIR" ]; then
    echo "[+] Extracting GCC..."
    tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"
else
    echo "[=] Source already extracted: $SOURCE_DIR"
fi

# ---------------------------------------------------------------------------
# Download GCC prerequisites (gmp, mpfr, mpc) into the source tree
# ---------------------------------------------------------------------------

echo "[+] Checking GCC prerequisites (gmp, mpfr, mpc, isl)..."
cd "$SOURCE_DIR"
if [ ! -d gmp ] || [ ! -d mpfr ] || [ ! -d mpc ]; then
    echo "[+] Downloading GCC prerequisites via download_prerequisites..."
    ./contrib/download_prerequisites --no-isl
    rm -rf gettext isl
else
    echo "[=] Prerequisites already present."
fi

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "[+] Configuring GCC pass 2..."

# CC_FOR_TARGET: use our own cross-gcc so pass2 knows where the sysroot is.
# --with-build-sysroot: where GCC looks for target headers during the BUILD
# (as opposed to --with-sysroot which is baked into the installed compiler).
CXX="g++ -std=c++17" \
CXXFLAGS_FOR_BUILD="-std=c++17" \
CC_FOR_TARGET="$KRATOS_TOOLS/bin/$TARGET-gcc" \
CXX_FOR_TARGET="$KRATOS_TOOLS/bin/$TARGET-g++" \
AR_FOR_TARGET="$KRATOS_TOOLS/bin/$TARGET-ar" \
RANLIB_FOR_TARGET="$KRATOS_TOOLS/bin/$TARGET-ranlib" \
"$SOURCE_DIR/configure"                 \
    --target="$TARGET"                  \
    --prefix="$KRATOS_TOOLS"            \
    --with-sysroot="$KRATOS_SYSROOT"    \
    --with-build-sysroot="$KRATOS_SYSROOT" \
    --enable-languages=c,c++            \
    --enable-shared                     \
    --enable-threads=posix              \
    --enable-__cxa_atexit               \
    --enable-clocale=gnu                \
    --disable-nls                       \
    --disable-multilib                  \
    --disable-libsanitizer              \
    --disable-libquadmath               \
    --disable-libvtv                    \
    --disable-libgomp                   \
    --with-system-zlib

echo "[+] Building GCC pass 2 (this may take a while)..."
make -j"$(nproc)" all-gcc all-target-libgcc all-target-libstdc++-v3

echo "[+] Installing GCC pass 2..."
make install-gcc install-target-libgcc install-target-libstdc++-v3

# ---------------------------------------------------------------------------
# Symlinks for convenience
# ---------------------------------------------------------------------------

echo "[+] Creating cc symlink..."
ln -sfv "$TARGET-gcc" "$KRATOS_TOOLS/bin/$TARGET-cc" 2>/dev/null || true

# ---------------------------------------------------------------------------
# Sanity check
# ---------------------------------------------------------------------------

echo
echo "[~] Verifying GCC pass 2 installation..."

CC="$KRATOS_TOOLS/bin/$TARGET-gcc"
CXX="$KRATOS_TOOLS/bin/$TARGET-g++"

if [ ! -f "$CC" ]; then
    echo "[!] $CC not found — something went wrong."
    exit 1
fi
if [ ! -f "$CXX" ]; then
    echo "[!] $CXX not found — something went wrong."
    exit 1
fi

echo "  [✓] C compiler:   $($CC --version | head -1)"
echo "  [✓] C++ compiler: $($CXX --version | head -1)"

# Verify the compiler targets our triplet (not the host)
ACTUAL_TARGET="$($CC -dumpmachine)"
if [[ "$ACTUAL_TARGET" != "$TARGET" ]]; then
    echo "[!] Compiler targets '$ACTUAL_TARGET', expected '$TARGET'."
    exit 1
fi
echo "  [✓] Target triplet: $ACTUAL_TARGET"

echo
echo "[✓] GCC pass 2 installed successfully."
echo
echo "  $KRATOS_TOOLS/bin/$TARGET-gcc"
echo "  $KRATOS_TOOLS/bin/$TARGET-g++"
echo "  $KRATOS_TOOLS/$TARGET/lib/libstdc++.so"
echo
echo "Next step: verify-toolchain.sh"
