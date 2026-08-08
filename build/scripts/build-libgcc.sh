#!/usr/bin/env bash
# build-libgcc.sh — Verify libgcc.a from GCC pass 1.
#
# Shared libgcc (libgcc_s.so) requires stdio.h from Glibc, which is not yet
# installed at this stage. Static libgcc.a was already built in GCC pass 1.
# Full libgcc_s.so will be built during GCC pass 2 after Glibc is installed.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

echo "========================================"
echo "        KRATOSOS LIBGCC"
echo "========================================"
echo
echo "  Target:    $TARGET"
echo "  Sysroot:   $KRATOS_SYSROOT"
echo

# Verify static libgcc.a from GCC pass 1 exists
LIBGCC_A="$(find "$KRATOS_TOOLS" -name "libgcc.a" 2>/dev/null | head -1 || true)"

if [ -n "$LIBGCC_A" ] && [ -f "$LIBGCC_A" ]; then
    echo "  [✓] Static libgcc.a verified: $LIBGCC_A"
else
    echo "[!] Static libgcc.a not found in $KRATOS_TOOLS"
    echo "    Run build-gcc-pass1.sh first."
    exit 1
fi

echo
echo "[✓] libgcc bootstrap stage complete (using static libgcc.a)."
echo "    Shared libgcc_s.so will be built in GCC pass 2 after Glibc."
