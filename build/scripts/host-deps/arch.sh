#!/usr/bin/env bash
# host-deps/arch.sh — Host dependency installer for Arch / Manjaro / EndeavourOS.
#
# Sourced by install-host-deps.sh. Requires pacman and root.
# Note: mbedTLS is NOT listed here — install-packages.sh builds a host-native
# version from source (isolated in $KRATOS_WORK/mbedtls-host-install) to avoid
# ABI conflicts with whatever version the distro ships.

ARCH_PACKAGES=(
    # Core build toolchain (gcc, g++, make, ar, ranlib, …)
    base-devel

    # Parser generators — kernel Kconfig, glibc, gcc configure
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

    # Crypto / signing — kernel module signing, certs
    openssl

    # GCC math & compression libs
    gmp
    mpfr
    libmpc
    zlib
    zstd

    # ELF introspection — tools/objtool (ORC unwind tables), provides gelf.h
    libelf

    # Source fetch / decompress
    rsync
    curl
    xz
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
    grub
    xorriso
    mtools
    squashfs-tools

    # Build helpers
    cmake
    ninja
    meson
    pkgconf

    # Python (kernel Kconfig scripts)
    python

    # QEMU — full system + user-mode emulator
    qemu-system-x86
    qemu-user
    qemu-user-static-binfmt

    # OVMF UEFI firmware for QEMU UEFI boot testing
    edk2-ovmf
)

install_arch_deps() {
    echo "[+] Full system sync (pacman -Syu) — required on Arch to avoid partial upgrades..."
    pacman -Syu --needed --noconfirm "${ARCH_PACKAGES[@]}"
}
