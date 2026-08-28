#!/usr/bin/env bash
# check-host-deps.sh — Verify that the HOST has all tools needed to build KratosOS.
#
# This script ONLY checks — it does not install anything and does NOT require root.
# Run it to see whether your host machine is ready before starting a build.
#
# Usage:
#   ./build/scripts/check-host-deps.sh          # standalone
#   ./build.sh --check-host                     # via build.sh
#
# To install missing dependencies, run (as root):
#   sudo ./build/scripts/install-host-deps.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---------------------------------------------------------------------------
# Colors (disabled if not a terminal)
# ---------------------------------------------------------------------------

if [ -t 1 ]; then
    GREEN=$'\e[32m'
    RED=$'\e[31m'
    BOLD=$'\e[1m'
    RESET=$'\e[0m'
else
    GREEN='' RED='' BOLD='' RESET=''
fi

export GREEN RED BOLD RESET

# ---------------------------------------------------------------------------
# Detect OS
# ---------------------------------------------------------------------------

if [ -f /etc/os-release ]; then
    # shellcheck source=/dev/null
    . /etc/os-release
else
    ID="unknown"
    PRETTY_NAME="Unknown OS"
fi

# ---------------------------------------------------------------------------
# Load common checks
# ---------------------------------------------------------------------------

source "$SCRIPT_DIR/host-deps/common.sh"

# ---------------------------------------------------------------------------
# Banner
# ---------------------------------------------------------------------------

echo
echo "${BOLD}KratosOS Host Compatibility Check${RESET}"
echo "=================================="
echo "  Host OS:      ${PRETTY_NAME:-$ID}"
echo "  Architecture: $(uname -m)"
echo

# ---------------------------------------------------------------------------
# Run command checks
# ---------------------------------------------------------------------------

run_checks
CHECK_RC=$?

# ---------------------------------------------------------------------------
# Extra sanity: gelf.h reachable?
# ---------------------------------------------------------------------------

if [ $CHECK_RC -eq 0 ]; then
    check_gelf_h || CHECK_RC=1
fi

echo

if [ $CHECK_RC -ne 0 ]; then
    echo "  Run the following to install missing dependencies:"
    echo "    sudo ./build/scripts/install-host-deps.sh"
    echo
    exit 1
fi
