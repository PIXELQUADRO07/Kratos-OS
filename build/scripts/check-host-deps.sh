#!/usr/bin/env bash
# check-host-deps.sh — Verify/install HOST packages needed to build KratosOS.
#
# Arch Linux only. This is the dev-loop build script — once KratosOS reaches
# a shippable state, end users install from the built ISO, not by running
# this. This does NOT touch the KratosOS target (cross toolchain, sysroot,
# etc.); it only makes sure the machine you're building ON has what it
# needs: compilers, headers, and tools that build.sh's stages shell out to.
#
# Must run with sudo/root (it installs system packages). Safe to re-run.

set -euo pipefail

echo "========================================"
echo "   KratosOS — Host Dependency Check"
echo "========================================"
echo

if [ "$(id -u)" -ne 0 ]; then
    echo "[!] This script installs system packages and needs root."
    echo "    Re-run as: sudo bash $0"
    exit 1
fi

if [ -f /etc/os-release ]; then
    . /etc/os-release
else
    ID="unknown"
fi

case "$ID" in
    arch|manjaro|endeavouros)
        ;;
    *)
        echo "[!] This script only supports Arch-based distros (detected: ${PRETTY_NAME:-$ID})."
        echo "    KratosOS dev builds are done from Arch; other machines should"
        echo "    just flash the built ISO once one exists."
        exit 1
        ;;
esac

echo "[+] Detected distro: ${PRETTY_NAME:-$ID}"
echo

# ---------------------------------------------------------------------------
# Why each package is here (so future-you doesn't delete one by accident):
#
#   base-devel              gcc/make/etc for building the HOST bootstrap
#                            tools (cross-gcc needs a working host compiler
#                            to build itself)
#   bison, flex              kernel Kconfig parser, glibc, and other GNU builds
#   bc                        kernel build (Kconfig arithmetic, version.h)
#   openssl                   kernel build (certs, module/kernel signing bits)
#   libelf                    tools/objtool (ORC unwind tables) — provides
#                            gelf.h. THIS is the exact error we hit before.
#   rsync                     used by several build-*.sh scripts to populate sysroot
#   curl                      download.sh fetches all tarballs with curl
#   xz, bzip2, gzip            decompressing source tarballs
#   parted                     build-disk.sh: GPT partitioning
#   dosfstools                 build-disk.sh: mkfs.fat (ESP)
#   e2fsprogs                  build-disk.sh: mkfs.ext4 (root fs)
#   util-linux                 losetup, blkid (build-disk.sh's own host-side use)
#   udev                       udevadm settle (build-disk.sh waits on it)
#   edk2-ovmf                  OVMF firmware for QEMU UEFI boot testing
#   grub                       grub-install --target=x86_64-efi
#   qemu-system-x86,
#   qemu-user,
#   qemu-user-static-binfmt     run-qemu.sh / cross-arch bits
#   gawk, sed, m4               GNU build systems (glibc/gcc configure scripts)
#   autoconf, automake,
#   libtool, pkgconf             needed by userland package builds (Fase 9/10)
#   gperf                        required by glibc's build
#   texinfo                      required by some GNU package builds (doc gen)
#   python                       newer kernel Kconfig/tooling scripts
#   git                          if you ever pull sources via git instead of the zip
# ---------------------------------------------------------------------------

PACMAN_PACKAGES=(
    base-devel
    bison
    flex
    bc
    openssl
    libelf
    rsync
    curl
    xz
    bzip2
    gzip
    parted
    dosfstools
    e2fsprogs
    util-linux
    udev
    edk2-ovmf
    grub
    qemu-system-x86
    qemu-user
    qemu-user-static-binfmt
    gawk
    sed
    m4
    autoconf
    automake
    libtool
    pkgconf
    gperf
    texinfo
    python
    git
    xorriso
    cpio
    mtools
    mbedtls
    cmake
    ninja
    meson
)

# Full sync, not just -Sy: installing packages after only a partial index
# refresh is a known way to break an Arch system (mixed old/new binaries
# expecting different library ABIs). Always -Syu here.
echo "[+] Full system sync (pacman -Syu) — required on Arch, never partial-upgrade..."
pacman -Syu --needed --noconfirm "${PACMAN_PACKAGES[@]}"

echo
echo "[✓] Host dependencies installed."
echo
echo "[+] Sanity-checking the one that bit us before (libelf/gelf.h)..."
if echo '#include <gelf.h>' | ${CC:-cc} -E - > /dev/null 2>&1; then
    echo "[✓] gelf.h is reachable."
else
    echo "[!] gelf.h still not found — check 'pacman -Qi libelf' above."
    exit 1
fi
