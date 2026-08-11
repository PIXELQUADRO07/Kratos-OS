#!/usr/bin/env bash
# build.sh — KratosOS master build script
#
# Runs all build phases in order with incremental build detection.
# Each stage is stamped on completion; re-running skips already-built stages.
#
# Usage:
#   ./build.sh              # full build (skip already-done stages)
#   ./build.sh --clean      # wipe stamps and rebuild everything
#   ./build.sh --force      # same as --clean
#   ./build.sh --list       # show all stages and their status
#   ./build.sh --from=STAGE # restart from a specific stage name
#
# Individual overrides (env):
#   KRATOS_JOBS=N   parallel make jobs (default: nproc)
#   SKIP_DISK=1     skip the final image/disk step
#
# All stages must be run as an unprivileged user except disk (requires sudo).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_SCRIPTS="$SCRIPT_DIR/build/scripts"
BUILD_DIR="$SCRIPT_DIR/build"
STAMP_DIR="$BUILD_DIR/.stamps"

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------

CLEAN=false
LIST_ONLY=false
FROM_STAGE=""

for arg in "$@"; do
    case "$arg" in
        --clean|--force) CLEAN=true ;;
        --list)          LIST_ONLY=true ;;
        --from=*)        FROM_STAGE="${arg#--from=}" ;;
        -h|--help)
            sed -n '2,20p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *)
            echo "[!] Unknown argument: $arg"
            echo "    Run '$0 --help' for usage."
            exit 1
            ;;
    esac
done

# ---------------------------------------------------------------------------
# Colors
# ---------------------------------------------------------------------------

BOLD=$'\e[1m'
GREEN=$'\e[32m'
YELLOW=$'\e[33m'
RED=$'\e[31m'
CYAN=$'\e[36m'
DIM=$'\e[2m'
RESET=$'\e[0m'

# ---------------------------------------------------------------------------
# Stage definitions
# ---------------------------------------------------------------------------
# Format: "name|script|needs_sudo|description"

declare -a STAGES=(
    "download|download.sh|no|Download all source tarballs"
    "bootstrap|bootstrap.sh|no|Initialise build directories"
    "linux-headers|install-linux-headers.sh|no|Install Linux kernel headers"
    "binutils|build-binutils.sh|no|Binutils (cross as/ld/ar)"
    "gcc-pass1|build-gcc-pass1.sh|no|GCC pass 1 (C only, no libc)"
    "glibc-bootstrap|build-glibc-bootstrap.sh|no|Glibc bootstrap (crt*.o + stub libc)"
    "libgcc|build-libgcc.sh|no|libgcc (shared runtime)"
    "glibc|build-glibc.sh|no|Glibc (full)"
    "gcc-pass2|build-gcc-pass2.sh|no|GCC pass 2 (C + C++ + libstdc++)"
    "kernel|build-kernel.sh|no|Linux kernel (bzImage + modules)"
    "grub|build-grub.sh|no|GRUB 2 EFI bootloader"
    "ncurses|build-ncurses.sh|no|ncurses (terminal library)"
    "readline|build-readline.sh|no|readline (line editing)"
    "bash|build-bash.sh|no|Bash shell"
    "coreutils|build-coreutils.sh|no|coreutils (ls, cp, mv, ...)"
    "grep|build-grep.sh|no|grep"
    "sed|build-sed.sh|no|sed"
    "gawk|build-gawk.sh|no|gawk"
    "findutils|build-findutils.sh|no|findutils (find, xargs)"
    "diffutils|build-diffutils.sh|no|diffutils (diff, cmp)"
    "tar|build-tar.sh|no|tar"
    "gzip|build-gzip.sh|no|gzip"
    "xz|build-xz.sh|no|xz"
    "bzip2|build-bzip2.sh|no|bzip2"
    "file|build-file.sh|no|file (magic detection)"
    "mbedtls|build-mbedtls.sh|no|mbedTLS (TLS library)"
    "ca-certs|build-ca-certificates.sh|no|Mozilla CA certificate bundle"
    "etc|create-etc-skeleton.sh|no|/etc skeleton (fstab, passwd, ...)"
    "init|build-init.sh|no|init + shutdown + devd + login + passwd"
    "pkg|build-pkg.sh|no|kpm package manager"
    "fetch|build-fetch.sh|no|kratos-fetch HTTPS client"
    "disk|build-disk.sh|yes|Disk image (requires sudo)"
)

# ---------------------------------------------------------------------------
# Stamp helpers
# ---------------------------------------------------------------------------

mkdir -p "$STAMP_DIR"

stamp_file() { echo "$STAMP_DIR/$1.done"; }

is_done() {
    local stamp
    stamp="$(stamp_file "$1")"
    [ -f "$stamp" ]
}

mark_done() {
    local stamp
    stamp="$(stamp_file "$1")"
    date -u +"%Y-%m-%dT%H:%M:%SZ" > "$stamp"
}

mark_undone() {
    rm -f "$(stamp_file "$1")"
}

# ---------------------------------------------------------------------------
# Clean stamps
# ---------------------------------------------------------------------------

if $CLEAN; then
    echo "${YELLOW}[~] Removing all build stamps...${RESET}"
    rm -rf "$STAMP_DIR"
    mkdir -p "$STAMP_DIR"
    echo "${GREEN}[✓] Stamps cleared. All stages will be rebuilt.${RESET}"
    echo
fi

# ---------------------------------------------------------------------------
# --list: just print stage status and exit
# ---------------------------------------------------------------------------

if $LIST_ONLY; then
    echo
    echo "${BOLD}KratosOS Build Stages${RESET}"
    echo "──────────────────────────────────────────────────────"
    printf "  %-20s %-6s %s\n" "STAGE" "STATUS" "DESCRIPTION"
    echo "──────────────────────────────────────────────────────"
    for entry in "${STAGES[@]}"; do
        IFS='|' read -r name _script sudo desc <<< "$entry"
        if is_done "$name"; then
            ts="$(cat "$(stamp_file "$name")")"
            printf "  ${GREEN}%-20s${RESET} ${DIM}[done  %s]${RESET}  %s\n" "$name" "$ts" "$desc"
        else
            printf "  ${YELLOW}%-20s${RESET} [pending]            %s\n" "$name" "$desc"
        fi
    done
    echo
    exit 0
fi

# ---------------------------------------------------------------------------
# --from: clear stamps from that stage onward
# ---------------------------------------------------------------------------

if [ -n "$FROM_STAGE" ]; then
    found=false
    for entry in "${STAGES[@]}"; do
        IFS='|' read -r name _rest <<< "$entry"
        if $found || [ "$name" = "$FROM_STAGE" ]; then
            found=true
            mark_undone "$name"
        fi
    done
    if ! $found; then
        echo "[!] Unknown stage: '$FROM_STAGE'"
        echo "    Run '$0 --list' to see valid stage names."
        exit 1
    fi
    echo "${YELLOW}[~] Restarting from stage '$FROM_STAGE'.${RESET}"
    echo
fi

# ---------------------------------------------------------------------------
# Banner
# ---------------------------------------------------------------------------

echo
echo "${BOLD}${CYAN}════════════════════════════════════════════════════${RESET}"
echo "${BOLD}${CYAN}        KratosOS — Full Build System                ${RESET}"
echo "${BOLD}${CYAN}════════════════════════════════════════════════════${RESET}"
echo
echo "  Jobs:   ${KRATOS_JOBS:-$(nproc)}"
echo "  Stamps: $STAMP_DIR"
echo

export KRATOS_JOBS="${KRATOS_JOBS:-$(nproc)}"
export MAKEFLAGS="-j${KRATOS_JOBS}"

# ---------------------------------------------------------------------------
# Run stages
# ---------------------------------------------------------------------------

TOTAL="${#STAGES[@]}"
CURRENT=0
SKIPPED=0
RAN=0
T_GLOBAL_START="$(date +%s)"

for entry in "${STAGES[@]}"; do
    IFS='|' read -r name script sudo_needed desc <<< "$entry"
    CURRENT=$((CURRENT + 1))

    # Skip disk stage if SKIP_DISK is set
    if [ "${SKIP_DISK:-0}" = "1" ] && [ "$name" = "disk" ]; then
        echo "${DIM}  [skip] disk (SKIP_DISK=1)${RESET}"
        SKIPPED=$((SKIPPED + 1))
        continue
    fi

    # --- Incremental check ---
    if is_done "$name"; then
        ts="$(cat "$(stamp_file "$name")")"
        printf "  ${GREEN}[✓]${RESET} ${DIM}%-20s already built (%s)${RESET}\n" "$name" "$ts"
        SKIPPED=$((SKIPPED + 1))
        continue
    fi

    # --- Stage header ---
    echo
    echo "${BOLD}${CYAN}──────────────────────────────────────────────────────${RESET}"
    printf "${BOLD}  [%d/%d] %s${RESET}\n" "$CURRENT" "$TOTAL" "$desc"
    echo "${BOLD}${CYAN}──────────────────────────────────────────────────────${RESET}"

    SCRIPT_PATH="$BUILD_SCRIPTS/$script"
    if [ ! -f "$SCRIPT_PATH" ]; then
        echo "${YELLOW}  [~] Script not found, skipping: $script${RESET}"
        SKIPPED=$((SKIPPED + 1))
        continue
    fi

    t_start="$(date +%s)"

    # Run with or without sudo
    if [ "$sudo_needed" = "yes" ]; then
        echo "  ${YELLOW}[!] This stage requires root. Running with sudo...${RESET}"
        sudo bash "$SCRIPT_PATH"
    else
        bash "$SCRIPT_PATH"
    fi

    t_end="$(date +%s)"
    elapsed=$((t_end - t_start))

    mark_done "$name"
    RAN=$((RAN + 1))

    echo
    printf "  ${GREEN}[✓] %-20s completed in %ds${RESET}\n" "$name" "$elapsed"
done

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

T_GLOBAL_END="$(date +%s)"
T_ELAPSED=$((T_GLOBAL_END - T_GLOBAL_START))

echo
echo "${BOLD}${CYAN}════════════════════════════════════════════════════${RESET}"
echo "${BOLD}${GREEN}  KratosOS Build Complete!${RESET}"
echo "${BOLD}${CYAN}════════════════════════════════════════════════════${RESET}"
echo
echo "  Stages run:     $RAN"
echo "  Stages skipped: $SKIPPED (already built)"
echo "  Total time:     ${T_ELAPSED}s ($(( T_ELAPSED/60 ))m $(( T_ELAPSED%60 ))s)"
echo
echo "  Image: $BUILD_DIR/images/kratosos.img"
echo
echo "  Test with QEMU + OVMF:"
echo
echo "    qemu-system-x86_64 \\"
echo "      -m 512M \\"
echo "      -drive file=\"$BUILD_DIR/images/kratosos.img\",format=raw,if=virtio \\"
echo "      -bios /usr/share/ovmf/OVMF.fd \\"
echo "      -nographic"
echo
