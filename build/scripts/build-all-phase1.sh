#!/usr/bin/env bash
# build-all-phase1.sh — Orchestrate the full KratosOS Phase 1 toolchain bootstrap.
#
# Executes all Phase 1 scripts in the correct dependency order, stopping
# immediately on any failure and reporting how far the build got.
#
# Usage:
#   ./build-all-phase1.sh           # full bootstrap from scratch
#   ./build-all-phase1.sh --verify  # only run verify-toolchain at the end
#
# Environment:
#   KRATOS_JOBS   override nproc for parallel make (default: nproc)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

VERIFY_ONLY=false
[[ "${1:-}" == "--verify" ]] && VERIFY_ONLY=true

export MAKEFLAGS="-j${KRATOS_JOBS:-$(nproc)}"

# ---------------------------------------------------------------------------
# Phase 1 stages (in dependency order)
# ---------------------------------------------------------------------------

declare -a STAGES=(
    "download.sh"
    "bootstrap.sh"
    "install-linux-headers.sh"
    "build-binutils.sh"
    "build-gcc-pass1.sh"
    "build-glibc-bootstrap.sh"
    "build-libgcc.sh"
    "build-glibc.sh"
    "build-gcc-pass2.sh"
    "verify-toolchain.sh"
)

STAGE_NAMES=(
    "Download sources"
    "Init build directories"
    "Linux kernel headers"
    "Binutils (cross as/ld/ar)"
    "GCC pass 1 (C only, no libc)"
    "Glibc bootstrap (crt*.o + stub libc)"
    "libgcc (shared runtime)"
    "Glibc (full)"
    "GCC pass 2 (C + C++ + libstdc++)"
    "Verify toolchain"
)

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

BOLD=$'\e[1m'
GREEN=$'\e[32m'
RED=$'\e[31m'
YELLOW=$'\e[33m'
RESET=$'\e[0m'

header() {
    echo
    echo "${BOLD}════════════════════════════════════════════${RESET}"
    echo "${BOLD}  $1${RESET}"
    echo "${BOLD}════════════════════════════════════════════${RESET}"
}

stage_header() {
    local n="$1" total="$2" name="$3"
    echo
    echo "${BOLD}${YELLOW}[Phase 1 — $n/$total] $name${RESET}"
    echo "────────────────────────────────────────────"
}

# Time a stage — NOTE: must explicitly capture rc before the echo,
# because set -e is suspended inside `if ! run_stage` and the echo
# would otherwise become the function's return value (always 0).
run_stage() {
    local script="$SCRIPT_DIR/$1"
    local t_start t_end elapsed rc

    t_start="$(date +%s)"
    bash "$script"
    rc=$?
    t_end="$(date +%s)"
    elapsed=$((t_end - t_start))

    if [ "$rc" -eq 0 ]; then
        echo "${GREEN}  ✓ Done in ${elapsed}s${RESET}"
    else
        echo "${RED}  ✗ Failed after ${elapsed}s (exit $rc)${RESET}"
    fi
    return "$rc"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

header "KratosOS Phase 1 — Toolchain Bootstrap"
echo
echo "  Target:    $TARGET"
echo "  Sysroot:   $KRATOS_SYSROOT"
echo "  Tools:     $KRATOS_TOOLS"
echo "  Jobs:      ${KRATOS_JOBS:-$(nproc)}"
echo

if $VERIFY_ONLY; then
    header "Running verify-toolchain.sh only"
    bash "$SCRIPT_DIR/verify-toolchain.sh"
    exit $?
fi

TOTAL="${#STAGES[@]}"
COMPLETED=0
T_TOTAL_START="$(date +%s)"

for i in "${!STAGES[@]}"; do
    script="${STAGES[$i]}"
    name="${STAGE_NAMES[$i]}"
    n=$((i+1))

    stage_header "$n" "$TOTAL" "$name"

    if ! run_stage "$script"; then
        echo
        echo "${RED}${BOLD}[✗] Phase 1 FAILED at stage $n/$TOTAL: $name${RESET}"
        echo "${RED}    Script: $SCRIPT_DIR/$script${RESET}"
        echo
        echo "  Completed stages: $COMPLETED / $TOTAL"
        exit 1
    fi

    COMPLETED=$((COMPLETED+1))
done

T_TOTAL_END="$(date +%s)"
T_ELAPSED=$((T_TOTAL_END - T_TOTAL_START))

echo
header "Phase 1 Complete"
echo
echo "  ${GREEN}${BOLD}✓ All $TOTAL stages completed successfully!${RESET}"
echo "  Total time: ${T_ELAPSED}s ($(( T_ELAPSED/60 ))m $(( T_ELAPSED%60 ))s)"
echo
echo "  Cross-compiler:"
echo "    $KRATOS_TOOLS/bin/$TARGET-gcc"
echo "    $KRATOS_TOOLS/bin/$TARGET-g++"
echo
echo "  Sysroot: $KRATOS_SYSROOT"
echo
echo "  ${BOLD}Next: Phase 2 — Userspace Base (bash, coreutils, ...)${RESET}"
echo
