#!/usr/bin/env bash
# install-host-deps.sh — Install HOST packages needed to build KratosOS.
#
# Detects the host distribution and installs the appropriate packages using
# the native package manager. Must be run as root.
#
# Supported hosts:
#   Arch Linux / Manjaro / EndeavourOS  → pacman
#   Fedora                              → dnf
#   Debian / Ubuntu                     → apt-get  (Phase 2, stub)
#
# Usage:
#   sudo ./build/scripts/install-host-deps.sh
#
# After installation, verify with:
#   ./build/scripts/check-host-deps.sh
#   ./build.sh --check-host

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---------------------------------------------------------------------------
# Colors
# ---------------------------------------------------------------------------

if [ -t 1 ]; then
    GREEN=$'\e[32m'
    RED=$'\e[31m'
    YELLOW=$'\e[33m'
    BOLD=$'\e[1m'
    RESET=$'\e[0m'
else
    GREEN='' RED='' YELLOW='' BOLD='' RESET=''
fi

export GREEN RED YELLOW BOLD RESET

# ---------------------------------------------------------------------------
# Must run as root
# ---------------------------------------------------------------------------

if [ "$(id -u)" -ne 0 ]; then
    echo "[!] This script installs system packages and needs root."
    echo "    Re-run as: sudo bash $0"
    exit 1
fi

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
echo "${BOLD}KratosOS — Host Dependency Installer${RESET}"
echo "====================================="
echo "  Host OS: ${PRETTY_NAME:-$ID}"
echo "  Arch:    $(uname -m)"
echo

# ---------------------------------------------------------------------------
# Dispatch to the right backend
# ---------------------------------------------------------------------------

case "${ID:-unknown}" in
    arch|manjaro|endeavouros)
        echo "[+] Detected Arch-based distro — using pacman backend."
        source "$SCRIPT_DIR/host-deps/arch.sh"
        install_arch_deps
        ;;
    fedora)
        echo "[+] Detected Fedora — using dnf backend."
        source "$SCRIPT_DIR/host-deps/fedora.sh"
        install_fedora_deps
        ;;
    debian|ubuntu|linuxmint|pop)
        echo "[+] Detected Debian/Ubuntu-based distro — using apt backend."
        echo "${YELLOW}[~] Note: Debian/Ubuntu support is Phase 2 (not yet fully tested).${RESET}"
        source "$SCRIPT_DIR/host-deps/debian.sh"
        install_debian_deps
        ;;
    *)
        echo "[!] Unsupported distribution: ${PRETTY_NAME:-$ID}"
        echo
        echo "    KratosOS requires the following commands to be present:"
        echo
        source "$SCRIPT_DIR/host-deps/common.sh"
        for cmd in "${HOST_COMMANDS[@]}"; do
            echo "      $cmd"
        done
        echo
        echo "    Install them using your distribution's package manager,"
        echo "    then verify with: ./build/scripts/check-host-deps.sh"
        exit 1
        ;;
esac

# ---------------------------------------------------------------------------
# Post-install verification
# ---------------------------------------------------------------------------

echo
echo "[+] Running post-install verification..."
echo

if run_checks && check_gelf_h; then
    echo
    echo "${GREEN}[✓] All host dependencies installed and verified.${RESET}"
    echo
    echo "    You can now start the build with:"
    echo "      ./build.sh"
    echo "    or check host compatibility at any time with:"
    echo "      ./build.sh --check-host"
else
    echo
    echo "${RED}[!] Some dependencies are still missing after installation.${RESET}"
    echo "    Check the output above and install them manually."
    exit 1
fi
