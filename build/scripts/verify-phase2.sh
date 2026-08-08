#!/usr/bin/env bash
# verify-phase2.sh — Verify the KratosOS Phase 2 userspace base.
#
# Checks that all expected binaries and libraries are present in the sysroot
# and that they are correctly built for the target architecture.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

SYSROOT="$KRATOS_SYSROOT"
CROSS="$KRATOS_TOOLS/bin/$TARGET"
PASS=0
FAIL=0

pass() { echo "  [PASS] $1"; PASS=$((PASS + 1)); }
fail() { echo "  [FAIL] $1"; FAIL=$((FAIL + 1)); }

check_binary() {
    local path="$SYSROOT/$1"
    if [ -f "$path" ] || [ -L "$path" ]; then
        pass "$1 exists"
    else
        fail "$1 MISSING"
    fi
}

check_elf() {
    local path="$SYSROOT/$1"
    if [ -f "$path" ] && file "$path" 2>/dev/null | grep -q "ELF 64-bit"; then
        pass "$1 is ELF64"
    elif [ -L "$path" ]; then
        pass "$1 is symlink"
    else
        fail "$1 not ELF64 or missing"
    fi
}

check_lib() {
    local found=false
    for dir in usr/lib usr/lib64 lib lib64; do
        if [ -f "$SYSROOT/$dir/$1" ] || [ -L "$SYSROOT/$dir/$1" ]; then
            found=true
            break
        fi
    done
    if $found; then
        pass "lib $1 present"
    else
        fail "lib $1 MISSING"
    fi
}

echo
echo "────────────────────────────────────────────"
echo "  1. Shell"
echo "────────────────────────────────────────────"
check_elf  "bin/bash"
check_binary "bin/sh"

echo
echo "────────────────────────────────────────────"
echo "  2. Coreutils (essential)"
echo "────────────────────────────────────────────"
for bin in ls cp mv rm cat echo mkdir chmod chown ln stat touch date du df; do
    check_binary "usr/bin/$bin"
done

echo
echo "────────────────────────────────────────────"
echo "  3. Text processing"
echo "────────────────────────────────────────────"
check_elf "usr/bin/grep"
check_elf "usr/bin/sed"
check_elf "usr/bin/gawk"

echo
echo "────────────────────────────────────────────"
echo "  4. Search & comparison"
echo "────────────────────────────────────────────"
check_elf "usr/bin/find"
check_elf "usr/bin/xargs"
check_elf "usr/bin/diff"
check_elf "usr/bin/cmp"

echo
echo "────────────────────────────────────────────"
echo "  5. Compression"
echo "────────────────────────────────────────────"
check_elf "usr/bin/tar"
check_elf "usr/bin/gzip"
check_elf "usr/bin/xz"
check_binary "usr/bin/bzip2"

echo
echo "────────────────────────────────────────────"
echo "  6. Utilities"
echo "────────────────────────────────────────────"
check_elf "usr/bin/file"

echo
echo "────────────────────────────────────────────"
echo "  7. Libraries"
echo "────────────────────────────────────────────"
check_lib "libncursesw.so"
check_lib "libreadline.so"

echo
echo "────────────────────────────────────────────"
echo "  8. Architecture check (sample ELF binaries)"
echo "────────────────────────────────────────────"
for bin in bin/bash usr/bin/ls usr/bin/grep usr/bin/sed usr/bin/tar; do
    if [ -f "$SYSROOT/$bin" ]; then
        arch=$(file "$SYSROOT/$bin" 2>/dev/null | grep -o "x86-64\|x86_64" || true)
        if [ -n "$arch" ]; then
            pass "$bin → $arch"
        else
            fail "$bin — unexpected arch ($(file "$SYSROOT/$bin" 2>/dev/null || echo 'unknown'))"
        fi
    fi
done

echo
echo "=========================================="
echo "       PHASE 2 VERIFICATION SUMMARY"
echo "=========================================="
echo
echo "  Passed: $PASS"
echo "  Failed: $FAIL"
echo

if [ "$FAIL" -eq 0 ]; then
    echo "[✓] Phase 2 userspace base is complete!"
    echo "    Next: Phase 3 — Kernel + Init + Bootloader"
    exit 0
else
    echo "[✗] $FAIL check(s) failed. Review the output above."
    exit 1
fi
