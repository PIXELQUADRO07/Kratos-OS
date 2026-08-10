#!/usr/bin/env bash
# build-all-phase3.sh — Orchestrate the full KratosOS Phase 3 build.
#
# Phase 3 covers everything needed to turn the Phase 2 userspace into a
# bootable disk image:
#
#   1. Linux kernel  (bzImage + modules)
#   2. GRUB 2 EFI    (modules + EFI binary skeleton in sysroot)
#   3. /etc skeleton (fstab, passwd, shadow, group, rc.d, ...)
#   4. init system   (init, shutdown, devd, kratos-net, login, passwd)
#   5. kpm           (Kratos Package Manager)
#   6. disk image    (GPT + ESP + root ext4, GRUB installed, grub.cfg)
#
# Usage:
#   ./build-all-phase3.sh        # run all steps
#   sudo ./build-all-phase3.sh   # or let the disk step escalate automatically
#
# Environment:
#   KRATOS_JOBS   override nproc for parallel make (default: nproc)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

export MAKEFLAGS="-j${KRATOS_JOBS:-$(nproc)}"

# ---------------------------------------------------------------------------
# Phase 3 stages (in dependency order)
# ---------------------------------------------------------------------------

declare -a STAGES=(
    "build-kernel.sh"
    "build-grub.sh"
    "create-etc-skeleton.sh"
    "build-init.sh"
    "build-pkg.sh"
    "build-disk.sh"
)

STAGE_NAMES=(
    "Linux kernel (bzImage + modules)"
    "GRUB 2 EFI bootloader"
    "/etc skeleton (fstab, passwd, rc.d, ...)"
    "init system (init, shutdown, devd, login, passwd)"
    "kpm package manager"
    "Disk image (GPT + ESP + root ext4)"
)

# The disk step requires root.
STAGE_SUDO=(no no no no no yes)

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

BOLD=$'\e[1m'
GREEN=$'\e[32m'
RED=$'\e[31m'
YELLOW=$'\e[33m'
CYAN=$'\e[36m'
RESET=$'\e[0m'

header() {
    echo
    echo "${BOLD}${CYAN}════════════════════════════════════════════${RESET}"
    echo "${BOLD}${CYAN}  $1${RESET}"
    echo "${BOLD}${CYAN}════════════════════════════════════════════${RESET}"
}

stage_header() {
    local n="$1" total="$2" name="$3"
    echo
    echo "${BOLD}${YELLOW}[Phase 3 — $n/$total] $name${RESET}"
    echo "────────────────────────────────────────────"
}

run_stage() {
    local script="$SCRIPT_DIR/$1"
    local use_sudo="$2"
    local t_start t_end elapsed rc

    t_start="$(date +%s)"

    if [ "$use_sudo" = "yes" ] && [ "$(id -u)" -ne 0 ]; then
        echo "${YELLOW}  [!] This stage requires root — invoking sudo...${RESET}"
        sudo bash "$script"
    else
        bash "$script"
    fi

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

header "KratosOS Phase 3 — Kernel, Bootloader, Init, Disk Image"
echo
echo "  Kernel version: $LINUX_VERSION"
echo "  GRUB version:   $GRUB_VERSION"
echo "  Target:         $TARGET"
echo "  Sysroot:        $KRATOS_SYSROOT"
echo "  Jobs:           ${KRATOS_JOBS:-$(nproc)}"
echo

TOTAL="${#STAGES[@]}"
COMPLETED=0
T_TOTAL_START="$(date +%s)"

for i in "${!STAGES[@]}"; do
    script="${STAGES[$i]}"
    name="${STAGE_NAMES[$i]}"
    use_sudo="${STAGE_SUDO[$i]}"
    n=$((i+1))

    stage_header "$n" "$TOTAL" "$name"

    if ! run_stage "$script" "$use_sudo"; then
        echo
        echo "${RED}${BOLD}[✗] Phase 3 FAILED at stage $n/$TOTAL: $name${RESET}"
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
header "Phase 3 Complete"
echo
echo "  ${GREEN}${BOLD}✓ All $TOTAL stages completed successfully!${RESET}"
echo "  Total time: ${T_ELAPSED}s ($(( T_ELAPSED/60 ))m $(( T_ELAPSED%60 ))s)"
echo
echo "  Bootable image: $KRATOS_ROOT/build/images/kratosos.img"
echo
echo "  Boot chain:"
echo "    UEFI firmware"
echo "    └── EFI/BOOT/BOOTX64.EFI   [ESP, FAT32]"
echo "    └── /boot/grub/grub.cfg    [root, ext4]"
echo "    └── /boot/vmlinuz          [kernel $LINUX_VERSION]"
echo "    └── /sbin/init"
echo "    └── /bin/bash"
echo
echo "  ${BOLD}Test with QEMU + OVMF:${RESET}"
echo
echo "    qemu-system-x86_64 \\"
echo "      -m 512M \\"
echo "      -drive file=\"$KRATOS_ROOT/build/images/kratosos.img\",format=raw,if=virtio \\"
echo "      -bios /usr/share/ovmf/OVMF.fd \\"
echo "      -nographic"
echo
