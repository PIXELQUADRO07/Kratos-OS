#!/usr/bin/env bash
# verify-toolchain.sh — Smoke-test the KratosOS Phase 1 cross-toolchain.
#
# Runs a series of compile/link tests to confirm that:
#   - The cross C and C++ compilers work
#   - They target the correct triplet
#   - They link against the KratosOS sysroot (not the host system)
#   - libgcc_s and libstdc++ are reachable
#   - (Optional) The resulting binary runs correctly via qemu-user
#
# Usage:
#   ./verify-toolchain.sh           # compile + inspect output
#   ./verify-toolchain.sh --run     # also execute via qemu-user-static

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

RUN_BINARY=false
[[ "${1:-}" == "--run" ]] && RUN_BINARY=true

CC="$KRATOS_TOOLS/bin/$TARGET-gcc"
CXX="$KRATOS_TOOLS/bin/$TARGET-g++"
READELF="$KRATOS_TOOLS/bin/$TARGET-readelf"
OBJDUMP="$KRATOS_TOOLS/bin/$TARGET-objdump"

TMPDIR="$KRATOS_WORK/toolchain-verify"
rm -rf "$TMPDIR"
mkdir -p "$TMPDIR"

PASS=0
FAIL=0

# ---------------------------------------------------------------------------
# Helper: print test result
# ---------------------------------------------------------------------------

pass() { echo "  [PASS] $1"; PASS=$((PASS+1)); }
fail() { echo "  [FAIL] $1"; FAIL=$((FAIL+1)); }

section() {
    echo
    echo "──────────────────────────────────────────"
    echo "  $1"
    echo "──────────────────────────────────────────"
}

# ---------------------------------------------------------------------------
# 1. Tool presence
# ---------------------------------------------------------------------------

section "1. Tool presence"

for tool in "$CC" "$CXX" \
    "$KRATOS_TOOLS/bin/$TARGET-as" \
    "$KRATOS_TOOLS/bin/$TARGET-ld" \
    "$KRATOS_TOOLS/bin/$TARGET-ar" \
    "$KRATOS_TOOLS/bin/$TARGET-readelf" \
    "$KRATOS_TOOLS/bin/$TARGET-objcopy" \
    "$KRATOS_TOOLS/bin/$TARGET-strip"; do

    name="$(basename "$tool")"
    if [ -f "$tool" ]; then
        pass "$name exists"
    else
        fail "$name NOT FOUND at $tool"
    fi
done

# ---------------------------------------------------------------------------
# 2. Target triplet check
# ---------------------------------------------------------------------------

section "2. Target triplet"

ACTUAL_CC_TARGET="$($CC -dumpmachine 2>/dev/null || echo FAIL)"
if [[ "$ACTUAL_CC_TARGET" == "$TARGET" ]]; then
    pass "gcc targets $TARGET"
else
    fail "gcc targets '$ACTUAL_CC_TARGET' (expected '$TARGET')"
fi

ACTUAL_CXX_TARGET="$($CXX -dumpmachine 2>/dev/null || echo FAIL)"
if [[ "$ACTUAL_CXX_TARGET" == "$TARGET" ]]; then
    pass "g++ targets $TARGET"
else
    fail "g++ targets '$ACTUAL_CXX_TARGET' (expected '$TARGET')"
fi

# ---------------------------------------------------------------------------
# 3. Compile a C hello world (static)
# ---------------------------------------------------------------------------

section "3. C hello world (static)"

cat > "$TMPDIR/hello_c.c" <<'EOF'
#include <stdio.h>
int main(void) {
    puts("KratosOS toolchain: C OK");
    return 0;
}
EOF

if "$CC" -static -o "$TMPDIR/hello_c_static" "$TMPDIR/hello_c.c" 2>"$TMPDIR/hello_c_static.err"; then
    pass "C static compile succeeded"
    ARCH="$(file "$TMPDIR/hello_c_static" | grep -o 'x86-64\|x86_64\|ELF 64-bit' || true)"
    if [[ -n "$ARCH" ]]; then
        pass "Binary is ELF (arch: $ARCH)"
    else
        fail "Binary arch check: $(file "$TMPDIR/hello_c_static")"
    fi
else
    fail "C static compile FAILED"
    cat "$TMPDIR/hello_c_static.err"
fi

# ---------------------------------------------------------------------------
# 4. Compile a C hello world (dynamic) — needs sysroot libs
# ---------------------------------------------------------------------------

section "4. C hello world (dynamic)"

if "$CC" -o "$TMPDIR/hello_c_dyn" "$TMPDIR/hello_c.c" \
        --sysroot="$KRATOS_SYSROOT" 2>"$TMPDIR/hello_c_dyn.err"; then
    pass "C dynamic compile succeeded"

    # Check dynamic linker points to our sysroot, not /lib64/ld-linux-x86-64.so.2
    INTERP="$("$READELF" -l "$TMPDIR/hello_c_dyn" 2>/dev/null | \
              grep 'interpreter' | grep -o '\[.*\]' | tr -d '[]' || true)"
    if [[ -n "$INTERP" ]]; then
        pass "Dynamic linker: $INTERP"
        # Just sanity-check it's not the host's /lib64 path
        if [[ "$INTERP" == "/lib64/ld-linux-x86-64.so.2" ]]; then
            fail "Linker is the HOST's ld — sysroot not applied correctly!"
        fi
    else
        fail "Could not read ELF interpreter (readelf failed?)"
    fi
else
    fail "C dynamic compile FAILED (missing sysroot libs?)"
    cat "$TMPDIR/hello_c_dyn.err" | head -20
fi

# ---------------------------------------------------------------------------
# 5. Compile a C++ hello world (static)
# ---------------------------------------------------------------------------

section "5. C++ hello world (static)"

cat > "$TMPDIR/hello_cxx.cc" <<'EOF'
#include <iostream>
#include <string>
int main() {
    std::string msg = "KratosOS toolchain: C++ OK";
    std::cout << msg << std::endl;
    return 0;
}
EOF

if "$CXX" -static -o "$TMPDIR/hello_cxx_static" "$TMPDIR/hello_cxx.cc" \
        -static-libstdc++ -static-libgcc 2>"$TMPDIR/hello_cxx_static.err"; then
    pass "C++ static compile succeeded"
else
    fail "C++ static compile FAILED"
    cat "$TMPDIR/hello_cxx_static.err" | head -20
fi

# ---------------------------------------------------------------------------
# 6. Sysroot library check
# ---------------------------------------------------------------------------

section "6. Sysroot library check"

for lib in \
    "$KRATOS_SYSROOT/usr/lib/libc.so" \
    "$KRATOS_SYSROOT/usr/lib/libc.a" \
    "$KRATOS_SYSROOT/usr/lib/crt1.o" \
    "$KRATOS_SYSROOT/usr/lib/crti.o" \
    "$KRATOS_SYSROOT/usr/lib/crtn.o" \
    "$KRATOS_SYSROOT/usr/include/stdio.h" \
    "$KRATOS_SYSROOT/usr/include/stdlib.h"; do

    name="${lib#$KRATOS_SYSROOT/}"
    if [ -f "$lib" ]; then
        pass "$name"
    else
        fail "$name NOT FOUND"
    fi
done

# libgcc_s
if ls "$KRATOS_TOOLS/$TARGET/lib/libgcc_s.so"* &>/dev/null; then
    pass "libgcc_s.so present"
else
    fail "libgcc_s.so NOT FOUND in $KRATOS_TOOLS/$TARGET/lib/"
fi

# libstdc++
if ls "$KRATOS_TOOLS/$TARGET/lib/libstdc++.so"* &>/dev/null; then
    pass "libstdc++.so present"
else
    fail "libstdc++.so NOT FOUND in $KRATOS_TOOLS/$TARGET/lib/"
fi

# ---------------------------------------------------------------------------
# 7. (Optional) Execute via qemu-user
# ---------------------------------------------------------------------------

section "7. Execute via qemu-user (optional)"

QEMU="$(command -v qemu-x86_64-static 2>/dev/null || \
        command -v qemu-x86_64 2>/dev/null || \
        echo "")"

if [ -z "$QEMU" ]; then
    echo "  [SKIP] qemu-x86_64-static not found — install qemu-user-static to enable"
elif ! $RUN_BINARY; then
    echo "  [SKIP] Pass --run flag to execute binaries via qemu"
else
    echo "  [~] Running C static binary via $QEMU..."
    OUTPUT="$($QEMU "$TMPDIR/hello_c_static" 2>&1 || true)"
    if [[ "$OUTPUT" == *"KratosOS toolchain: C OK"* ]]; then
        pass "C static binary executed correctly"
        echo "       Output: $OUTPUT"
    else
        fail "C static binary output unexpected: $OUTPUT"
    fi

    echo "  [~] Running C++ static binary via $QEMU..."
    OUTPUT="$($QEMU "$TMPDIR/hello_cxx_static" 2>&1 || true)"
    if [[ "$OUTPUT" == *"KratosOS toolchain: C++ OK"* ]]; then
        pass "C++ static binary executed correctly"
        echo "       Output: $OUTPUT"
    else
        fail "C++ static binary output unexpected: $OUTPUT"
    fi
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

echo
echo "=========================================="
echo "          VERIFICATION SUMMARY"
echo "=========================================="
echo
echo "  Passed: $PASS"
echo "  Failed: $FAIL"
echo

if [ "$FAIL" -eq 0 ]; then
    echo "[✓] Toolchain is fully functional. Phase 1 complete!"
    echo "    Next: Phase 2 — Userspace Base (bash, coreutils, ...)"
    exit 0
else
    echo "[✗] $FAIL test(s) failed. Fix the issues above before continuing."
    exit 1
fi
