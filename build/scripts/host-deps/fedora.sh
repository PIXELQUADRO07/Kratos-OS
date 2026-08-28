#!/usr/bin/env bash
# host-deps/fedora.sh — Host dependency installer for Fedora Linux.
#
# Sourced by install-host-deps.sh. Requires dnf and root.
#
# Package name mapping notes vs Arch:
#   Arch base-devel      → Fedora "Development Tools" group + gcc + gcc-c++
#   Arch libelf          → Fedora elfutils-libelf-devel  (provides gelf.h)
#   Arch pkgconf         → Fedora pkgconf-pkg-config
#   Arch python          → Fedora python3
#   Arch udev            → Fedora systemd-udev
#   Arch ninja           → Fedora ninja-build
#   Arch qemu-system-x86 → Fedora qemu-system-x86-core
#   Arch qemu-user +
#        qemu-user-static-binfmt → Fedora qemu-user-static  (binfmt included)
#   Arch edk2-ovmf       → Fedora edk2-ovmf  (same name)
#   Arch squashfs-tools  → Fedora squashfs-tools  (same name)
#   mbedTLS              → NOT listed (built from source by install-packages.sh)

FEDORA_GROUPS=(
    "Development Tools"
)

FEDORA_PACKAGES=(
    # Explicit compiler packages (also pulled by group, but be explicit)
    gcc
    gcc-c++
    make

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

    # GCC & Toolchain building libraries
    gmp-devel
    mpfr-devel
    libmpc-devel
    zlib-devel
    zstd-devel

    # Crypto / signing — kernel certs, module signing
    openssl
    openssl-devel

    # ELF introspection — provides gelf.h (equivalent of Arch's libelf)
    elfutils-libelf-devel

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
    systemd-udev

    # Bootloader + ISO
    grub2-tools
    grub2-tools-extra
    xorriso
    mtools
    squashfs-tools

    # Build helpers
    cmake
    ninja-build
    meson
    pkgconf-pkg-config

    # Python (kernel Kconfig / tooling scripts / Mesa code generator)
    python3
    python3-mako
    python3-pyyaml
    python3-pip

    # QEMU — full system emulator
    qemu-system-x86-core

    # QEMU — user-mode + binfmt (combined on Fedora)
    qemu-user-static

    # OVMF UEFI firmware for QEMU UEFI boot testing
    edk2-ovmf
)

install_fedora_deps() {
    echo "[+] Installing packages via dnf..."
    # Try installing development group if available (non-fatal if group name differs in DNF5)
    dnf group install -y development-tools 2>/dev/null || \
    dnf group install -y "Development Tools" 2>/dev/null || \
    dnf group install -y "C Development Tools and Libraries" 2>/dev/null || true

    echo "[+] Installing required packages..."
    dnf install -y "${FEDORA_PACKAGES[@]}"
}
