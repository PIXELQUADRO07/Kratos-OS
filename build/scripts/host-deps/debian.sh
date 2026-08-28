#!/usr/bin/env bash
# host-deps/debian.sh — Host dependency installer for Debian / Ubuntu.
#
# Sourced by install-host-deps.sh. Requires apt-get and root.
#
# Status: PHASE 2 STUB — functional but not yet tested on a live Debian/Ubuntu
# system. Package names are based on Debian 12 (Bookworm) / Ubuntu 24.04 LTS.
#
# Package name mapping notes vs Arch:
#   Arch base-devel      → Debian build-essential
#   Arch libelf          → Debian libelf-dev          (provides gelf.h)
#   Arch pkgconf         → Debian pkgconf
#   Arch python          → Debian python3
#   Arch udev            → Debian udev
#   Arch ninja           → Debian ninja-build
#   Arch qemu-system-x86 → Debian qemu-system-x86
#   Arch qemu-user +
#        qemu-user-static-binfmt → Debian qemu-user-static
#   Arch edk2-ovmf       → Debian ovmf
#   Arch squashfs-tools  → Debian squashfs-tools  (same)
#   mbedTLS              → NOT listed (built from source by install-packages.sh)

DEBIAN_PACKAGES=(
    # Core build toolchain
    build-essential

    # Parser generators
    bison
    flex

    # Misc GNU build requirements
    bc
    m4
    gawk
    sed
    autoconf
    automake
    libtool
    gperf
    texinfo

    # GCC math & compression libs
    libgmp-dev
    libmpfr-dev
    libmpc-dev
    zlib1g-dev
    libzstd-dev

    # Crypto / signing
    libssl-dev

    # ELF introspection — provides gelf.h
    libelf-dev

    # Source fetch / decompress
    rsync
    curl
    xz-utils
    bzip2
    gzip
    git
    cpio

    # Disk image utilities
    parted
    dosfstools
    e2fsprogs
    util-linux
    udev

    # Bootloader + ISO
    grub-common
    grub-efi-amd64-bin
    xorriso
    mtools
    squashfs-tools

    # Build helpers
    cmake
    ninja-build
    meson
    pkgconf

    # Python
    python3

    # QEMU — full system emulator
    qemu-system-x86

    # QEMU — user-mode + binfmt
    qemu-user-static

    # OVMF UEFI firmware
    ovmf
)

install_debian_deps() {
    echo "[+] Updating apt package index..."
    apt-get update -y

    echo "[+] Installing packages..."
    apt-get install -y "${DEBIAN_PACKAGES[@]}"
}
