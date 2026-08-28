#!/usr/bin/env bash
# host-deps/common.sh — Shared host dependency check logic (distro-agnostic).
#
# Sourced by check-host-deps.sh and install-host-deps.sh.
# Provides:
#   HOST_COMMANDS   — canonical list of commands the build system requires
#   require_cmd()   — check a single command; accumulate failures
#   run_checks()    — iterate HOST_COMMANDS and print a formatted report
#   check_gelf_h()  — sanity-check that gelf.h is reachable (historical bug)

# ---------------------------------------------------------------------------
# Canonical command list.
# Verified via command -v, not package name — works on any distro.
# ---------------------------------------------------------------------------

HOST_COMMANDS=(
    # Core build toolchain
    bash gcc g++ make bison flex gawk bc m4 sed

    # Source fetch / decompress
    curl tar xz gzip bzip2 rsync git cpio

    # Disk image utilities
    parted mkfs.fat mkfs.ext4 losetup blkid

    # Bootloader + ISO
    grub-install grub-mkrescue xorriso mksquashfs mtools

    # Build helpers
    cmake ninja python3

    # QEMU (full-system emulator — checked separately from qemu-user)
    qemu-system-x86_64
)

# ---------------------------------------------------------------------------
# Tracking state (reset by run_checks)
# ---------------------------------------------------------------------------

_MISSING_CMDS=()

# ---------------------------------------------------------------------------
# require_cmd <cmd> [hint]
#   Prints [✓] or [✗] and appends missing entries to _MISSING_CMDS.
# ---------------------------------------------------------------------------

require_cmd() {
    local cmd="$1"
    local hint="${2:-}"
    if command -v "$cmd" > /dev/null 2>&1; then
        printf "  ${GREEN:-}[✓]${RESET:-} %s\n" "$cmd"
    else
        if [ -n "$hint" ]; then
            printf "  ${RED:-}[✗]${RESET:-} %-28s  ← %s\n" "$cmd" "$hint"
        else
            printf "  ${RED:-}[✗]${RESET:-} %s\n" "$cmd"
        fi
        _MISSING_CMDS+=("$cmd")
    fi
}

# ---------------------------------------------------------------------------
# run_checks
#   Iterates HOST_COMMANDS, prints a compatibility report, returns 0 if all
#   present, 1 if anything is missing.
# ---------------------------------------------------------------------------

run_checks() {
    _MISSING_CMDS=()

    echo
    printf "  %-28s  %s\n" "Command" "Status"
    printf "  %-28s  %s\n" "-------" "------"

    for cmd in "${HOST_COMMANDS[@]}"; do
        require_cmd "$cmd"
    done

    # qemu-user: accept either qemu-x86_64-static or qemu-x86_64
    local qemu_user_ok=false
    for q in qemu-x86_64-static qemu-x86_64; do
        if command -v "$q" > /dev/null 2>&1; then
            printf "  ${GREEN:-}[✓]${RESET:-} qemu-user  (%s)\n" "$q"
            qemu_user_ok=true
            break
        fi
    done
    if ! $qemu_user_ok; then
        printf "  ${RED:-}[✗]${RESET:-} qemu-user  (qemu-x86_64-static or qemu-x86_64)\n"
        _MISSING_CMDS+=("qemu-user")
    fi

    echo

    if [ "${#_MISSING_CMDS[@]}" -eq 0 ]; then
        printf "  ${GREEN:-}[✓] Host is compatible with KratosOS build system.${RESET:-}\n"
        return 0
    else
        printf "  ${RED:-}[✗] Missing %d command(s): %s${RESET:-}\n" \
            "${#_MISSING_CMDS[@]}" "${_MISSING_CMDS[*]}"
        echo
        echo "  Run: sudo ./build/scripts/install-host-deps.sh"
        return 1
    fi
}

# ---------------------------------------------------------------------------
# check_gelf_h
#   Verifies that gelf.h is reachable via the host compiler.
#   (This was the specific bug that originally blocked the build on Arch
#   when libelf was not installed.)
# ---------------------------------------------------------------------------

check_gelf_h() {
    echo "[+] Sanity-checking gelf.h (elfutils/libelf-devel)..."
    if echo '#include <gelf.h>' | ${CC:-cc} -E - > /dev/null 2>&1; then
        echo "[✓] gelf.h is reachable."
        return 0
    else
        echo "[✗] gelf.h NOT found."
        echo "    Arch:   pacman -S libelf"
        echo "    Fedora: dnf install elfutils-libelf-devel"
        echo "    Debian: apt install libelf-dev"
        return 1
    fi
}
