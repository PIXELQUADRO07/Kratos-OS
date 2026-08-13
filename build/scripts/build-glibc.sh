#!/usr/bin/env bash
# build-glibc.sh — Build and install the complete glibc into the KratosOS sysroot.
#
# This is the FULL glibc build (not the bootstrap). It produces:
#   - libc.so.6, libc.a            (C library, shared + static)
#   - All glibc headers            (stdio.h, stdlib.h, ...)
#   - libm, libdl, libpthread, librt, libresolv, ...
#
# Prerequisites (must run in order):
#   1. install-linux-headers.sh    (kernel API headers in sysroot)
#   2. build-gcc-pass1.sh          (cross C compiler)
#   3. build-glibc-bootstrap.sh    (crt*.o + stub libc.so)
#   4. build-libgcc.sh             (libgcc_s.so in sysroot + tools)
#
# Output (all installed under $KRATOS_SYSROOT):
#   usr/include/   — complete glibc headers
#   usr/lib/libc.so.6, usr/lib/libc.a
#   usr/lib/libm.so.6, usr/lib/libpthread.so.0, ...

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

PACKAGE="glibc"
VERSION="$GLIBC_VERSION"

ARCHIVE="$KRATOS_DOWNLOADS/$PACKAGE-$VERSION.tar.xz"
SOURCE_DIR="$KRATOS_SOURCES/$PACKAGE-$VERSION"
BUILD_DIR="$KRATOS_WORK/$PACKAGE-full-build"

URL="https://ftp.gnu.org/gnu/glibc/glibc-$VERSION.tar.xz"

# Cross-compiler tools (explicit — do not rely solely on PATH)
CROSS_CC="$KRATOS_TOOLS/bin/$TARGET-gcc"
CROSS_CXX="$KRATOS_TOOLS/bin/$TARGET-g++"
CROSS_AR="$KRATOS_TOOLS/bin/$TARGET-ar"
CROSS_RANLIB="$KRATOS_TOOLS/bin/$TARGET-ranlib"

echo "========================================"
echo "        KRATOSOS GLIBC (FULL)"
echo "========================================"
echo
echo "  Version:  $VERSION"
echo "  Target:   $TARGET"
echo "  Sysroot:  $KRATOS_SYSROOT"
echo "  CC:       $CROSS_CC"
echo

# ---------------------------------------------------------------------------
# Prerequisite checks
# ---------------------------------------------------------------------------

echo "[~] Checking prerequisites..."

if [ ! -f "$CROSS_CC" ]; then
    echo "[!] Cross compiler not found: $CROSS_CC"
    echo "    Run build-gcc-pass1.sh first."
    exit 1
fi

for f in crt1.o crti.o crtn.o libc.so; do
    if [ ! -f "$KRATOS_SYSROOT/usr/lib/$f" ]; then
        echo "[!] Missing: $KRATOS_SYSROOT/usr/lib/$f"
        echo "    Run build-glibc-bootstrap.sh first."
        exit 1
    fi
done

if ! ls "$KRATOS_SYSROOT/usr/lib/libgcc_s.so"* &>/dev/null; then
    echo "  [~] Note: libgcc_s.so not in sysroot yet; glibc will link against static libgcc.a."
fi

echo "  [✓] All prerequisites satisfied."
echo

# ---------------------------------------------------------------------------
# Download
# ---------------------------------------------------------------------------

mkdir -p "$KRATOS_DOWNLOADS" "$KRATOS_SOURCES" "$KRATOS_WORK"

if [ ! -f "$ARCHIVE" ]; then
    echo "[+] Downloading glibc $VERSION..."
    curl -L --progress-bar --retry 3 "$URL" -o "$ARCHIVE.part"
    mv "$ARCHIVE.part" "$ARCHIVE"
else
    echo "[=] Archive already present."
fi

# ---------------------------------------------------------------------------
# Extract
# ---------------------------------------------------------------------------

if [ ! -d "$SOURCE_DIR" ]; then
    echo "[+] Extracting glibc..."
    tar -xf "$ARCHIVE" -C "$KRATOS_SOURCES"
else
    echo "[=] Source already extracted."
fi

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

BUILD_TRIPLET="$("$SOURCE_DIR/scripts/config.guess")"

echo "[+] Configuring glibc (full build)..."

CC="$CROSS_CC"                      \
CXX="$CROSS_CXX"                    \
AR="$CROSS_AR"                      \
RANLIB="$CROSS_RANLIB"              \
"$SOURCE_DIR/configure"             \
    --prefix=/usr                   \
    --build="$BUILD_TRIPLET"        \
    --host="$TARGET"                \
    --with-headers="$KRATOS_SYSROOT/usr/include" \
    --with-lib-prefix="$KRATOS_SYSROOT/usr" \
    --disable-nscd                  \
    --disable-werror                \
    --enable-kernel=5.4             \
    libc_cv_forced_unwind=yes       \
    libc_cv_c_cleanup=yes           \
    libc_cv_slibdir=/usr/lib

echo "[+] Building glibc (full)..."
make -j"$(nproc)"

echo "[+] Installing glibc into sysroot..."
make DESTDIR="$KRATOS_SYSROOT" install

# ---------------------------------------------------------------------------
# Post-install: create missing symlinks glibc doesn't always create
# ---------------------------------------------------------------------------

echo "[+] Post-install fixups..."

SYSROOT_LIB="$KRATOS_SYSROOT/usr/lib"

# glibc installs libc.so.6 and libc.a; create plain libc.so linker stub
# if it was overwritten by the install
if [ ! -f "$SYSROOT_LIB/libc.so" ]; then
    echo "  [+] Recreating libc.so linker stub..."
    cat > "$SYSROOT_LIB/libc.so" <<'LINKER_SCRIPT'
/* GNU ld script — allows -lc to find libc.so.6 */
OUTPUT_FORMAT(elf64-x86-64)
GROUP ( /usr/lib/libc.so.6 /usr/lib/libc_nonshared.a AS_NEEDED ( /usr/lib/libgcc_s.so ) )
LINKER_SCRIPT
fi

# CRITICAL: every dynamically-linked binary we build (bash, coreutils,
# init, ...) has "/lib64/ld-linux-x86-64.so.2" baked in as its ELF
# interpreter (PT_INTERP) — that's GCC's hardcoded default for the
# x86_64-*-linux-gnu target, completely independent of glibc's own
# libc_cv_slibdir=/usr/lib setting above, which only controls where
# glibc's *own* `make install` physically copies the loader. Without
# this symlink, execve() of every dynamic binary in the final image
# fails at runtime (kernel can't find the interpreter at the exact
# absolute path stored in the binary), even though everything links
# and builds without a single error along the way — a merged-usr
# compat symlink, same as Arch/Fedora ship, closes that gap.
if [ ! -e "$KRATOS_SYSROOT/lib64" ]; then
    echo "  [+] Creating /lib64 -> usr/lib compat symlink (required for dynamic linking to work at all)..."
    ln -sf usr/lib "$KRATOS_SYSROOT/lib64"
fi

# ---------------------------------------------------------------------------
# Verify
# ---------------------------------------------------------------------------

echo
echo "[~] Verifying glibc installation..."

ERRORS=0
for f in \
    "$SYSROOT_LIB/libc.so.6" \
    "$SYSROOT_LIB/libc.a" \
    "$SYSROOT_LIB/crt1.o" \
    "$KRATOS_SYSROOT/usr/include/stdio.h" \
    "$KRATOS_SYSROOT/usr/include/stdlib.h" \
    "$KRATOS_SYSROOT/usr/include/string.h"; do

    name="${f#$KRATOS_SYSROOT/}"
    if [ -f "$f" ]; then
        echo "  [✓] $name"
    else
        echo "  [✗] MISSING: $name"
        ERRORS=$((ERRORS+1))
    fi
done

if [ "$ERRORS" -gt 0 ]; then
    echo
    echo "[!] $ERRORS file(s) missing after install. Something went wrong."
    exit 1
fi

echo
echo "[✓] glibc installed successfully."
echo
echo "  Headers:  $KRATOS_SYSROOT/usr/include/"
echo "  Libs:     $KRATOS_SYSROOT/usr/lib/"
echo
echo "Next step: build-gcc-pass2.sh"
