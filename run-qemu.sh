#!/usr/bin/env bash
# run-qemu.sh — Boot KratosOS in QEMU/OVMF for local testing
#
# Usage:
#   ./run-qemu.sh                   # serial console (headless, default)
#   ./run-qemu.sh --graphic         # VGA window (needs display)
#   ./run-qemu.sh --graphic --kvm   # VGA + KVM hardware acceleration
#   ./run-qemu.sh --kvm             # serial + KVM
#   ./run-qemu.sh --mem 1G          # override RAM (default: 512M)
#   ./run-qemu.sh --image PATH      # use a specific image file
#   ./run-qemu.sh --dry-run         # print the qemu command without running
#
# Serial console mode (default, --nographic):
#   Ctrl-A X    → quit QEMU
#   Ctrl-A C    → switch to QEMU monitor
#   Ctrl-A H    → help
#
# Exit codes:
#   0   QEMU exited cleanly
#   1   prerequisite check failed

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_DEFAULT="$SCRIPT_DIR/build/images/kratosos.img"

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------

GRAPHIC=false
KVM=true
NO_KVM=false
MEM="512M"
IMAGE="$IMAGE_DEFAULT"
DRY_RUN=false

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------

while [[ $# -gt 0 ]]; do
    case "$1" in
        --graphic)        GRAPHIC=true        ;;
        --kvm)            KVM=true            ;;
        --no-kvm)         NO_KVM=true; KVM=false ;;
        --mem)            MEM="$2"; shift     ;;
        --image)          IMAGE="$2"; shift   ;;
        --dry-run)        DRY_RUN=true        ;;
        -h|--help)
            sed -n '2,20p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *)
            echo "[!] Unknown argument: $1"
            echo "    Run '$0 --help' for usage."
            exit 1
            ;;
    esac
    shift
done

# ---------------------------------------------------------------------------
# Colors
# ---------------------------------------------------------------------------

BOLD=$'\e[1m'
GREEN=$'\e[32m'
YELLOW=$'\e[33m'
RED=$'\e[31m'
CYAN=$'\e[36m'
RESET=$'\e[0m'

# ---------------------------------------------------------------------------
# Prerequisite checks
# ---------------------------------------------------------------------------

echo
echo "${BOLD}${CYAN}════════════════════════════════════════════${RESET}"
echo "${BOLD}${CYAN}      KratosOS QEMU Boot Test               ${RESET}"
echo "${BOLD}${CYAN}════════════════════════════════════════════${RESET}"
echo

# qemu-system-x86_64
if ! command -v qemu-system-x86_64 &>/dev/null; then
    echo "${RED}[!] qemu-system-x86_64 not found.${RESET}"
    echo "    Install with:"
    echo "      Arch:   sudo pacman -S qemu-full"
    echo "      Debian: sudo apt install qemu-system-x86"
    exit 1
fi

QEMU_VER="$(qemu-system-x86_64 --version | head -1)"
echo "  QEMU:   $QEMU_VER"

# OVMF firmware
OVMF_CANDIDATES=(
    /usr/share/edk2/x64/OVMF.4m.fd
    /usr/share/edk2/x64/OVMF_CODE.4m.fd
    /usr/share/edk2/x64/OVMF.fd
    /usr/share/ovmf/OVMF.fd
    /usr/share/OVMF/OVMF.fd
    /usr/share/qemu/OVMF.fd
    /usr/share/edk2-ovmf/x64/OVMF.fd
    /usr/share/edk2-ovmf/OVMF.fd
)

OVMF_PATH=""
for candidate in "${OVMF_CANDIDATES[@]}"; do
    if [ -f "$candidate" ]; then
        OVMF_PATH="$candidate"
        break
    fi
done

if [ -z "$OVMF_PATH" ]; then
    echo "${RED}[!] OVMF firmware not found.${RESET}"
    echo "    Install with:"
    echo "      Arch:   sudo pacman -S edk2-ovmf"
    echo "      Debian: sudo apt install ovmf"
    echo
    echo "    Then re-run this script."
    exit 1
fi

echo "  OVMF:   $OVMF_PATH"

# Disk image
if [ ! -f "$IMAGE" ]; then
    echo
    echo "${RED}[!] Disk image not found: $IMAGE${RESET}"
    echo
    echo "    Build it first with:"
    echo "      ./build.sh        (full build)"
    echo "      sudo make disk    (disk step only, requires built sysroot)"
    exit 1
fi

IMAGE_SIZE="$(du -sh "$IMAGE" | cut -f1)"
echo "  Image:  $IMAGE ($IMAGE_SIZE)"
echo "  RAM:    $MEM"

if $KVM; then
    if [ -w /dev/kvm ]; then
        echo "  KVM:    enabled"
    else
        echo "${YELLOW}  [~] KVM requested but /dev/kvm not accessible. Falling back to TCG.${RESET}"
        KVM=false
    fi
fi

echo

# ---------------------------------------------------------------------------
# Build QEMU command
# ---------------------------------------------------------------------------

CMD=(
    qemu-system-x86_64
    -m "$MEM"
    -drive "file=$IMAGE,format=raw,if=virtio"
    -bios "$OVMF_PATH"
)

if $KVM; then
    CMD+=(-enable-kvm -cpu host)
else
    CMD+=(-cpu qemu64)
fi

if $GRAPHIC; then
    CMD+=(-vga virtio)
    echo "${YELLOW}  VGA window mode. Close the window or press Ctrl-C to quit.${RESET}"
else
    CMD+=(-nographic)
    echo "  Serial console mode."
    echo "  ${BOLD}Quit:${RESET}  Ctrl-A X"
    echo "  ${BOLD}Monitor:${RESET} Ctrl-A C"
fi

# Serial port: forward to stdio (already done by -nographic; add explicitly
# for graphic mode so boot messages still appear on the terminal)
if $GRAPHIC; then
    CMD+=(-serial stdio)
fi

echo
echo "${BOLD}Command:${RESET}"
echo "  ${CMD[*]}"
echo

# ---------------------------------------------------------------------------
# Dry-run mode
# ---------------------------------------------------------------------------

if $DRY_RUN; then
    echo "${YELLOW}[dry-run] Not executing. Copy the command above to run manually.${RESET}"
    exit 0
fi

# ---------------------------------------------------------------------------
# Launch
# ---------------------------------------------------------------------------

echo "${GREEN}${BOLD}[+] Booting KratosOS...${RESET}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo

exec "${CMD[@]}"
