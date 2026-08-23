#!/usr/bin/env bash
# build-iso.sh — Create a bootable KratosOS EFI/BIOS hybrid ISO image
#
# Boot chain:
#   UEFI/BIOS → GRUB 2 (ISO) → /boot/grub/grub.cfg → loads /boot/vmlinuz + /boot/initramfs.cpio.gz
#   Kernel boots → unpacks initramfs as read-write rootfs → launches /sbin/init (PID 1)
#
# Usage:
#   ./build-iso.sh
#
# Note: This script requires 'grub-mkrescue' and 'xorriso' (usually provided by libisoburn).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

SYSROOT="$KRATOS_SYSROOT"
IMAGE_DIR="$KRATOS_ROOT/build/images"
ISO_OUT="$IMAGE_DIR/kratosos.iso"
ISO_ROOT="$KRATOS_WORK/iso_root"

# ------------------------------------------------------------
# Banner
# ------------------------------------------------------------
echo "========================================"
echo "         KRATOSOS ISO IMAGE BUILD"
echo "========================================"
echo "  Target:  $ISO_OUT"
echo "  Sysroot: $SYSROOT"
echo

# ------------------------------------------------------------
# Checks
# ------------------------------------------------------------
if [ ! -d "$SYSROOT" ] || [ ! -f "$SYSROOT/sbin/init" ]; then
    echo "[!] Error: Sysroot is not built or init is missing."
    echo "    Please run 'make all' or compile phase3 targets first."
    exit 1
fi

if ! command -v grub-mkrescue &>/dev/null || ! command -v xorriso &>/dev/null; then
    echo "[!] Error: 'grub-mkrescue' or 'xorriso' utility not found."
    echo "    Install missing host dependencies via:"
    echo "      Arch Linux:    sudo pacman -S --needed grub xorriso"
    echo "      Debian/Ubuntu: sudo apt install grub-common xorriso mtools"
    echo "      Fedora:        sudo dnf install grub2-tools xorriso mtools"
    exit 1
fi

# ------------------------------------------------------------
# Step 1: Sync latest Live, Desktop and Calamares configurations
# ------------------------------------------------------------
echo "[Step 1] Syncing latest Live, Desktop and Calamares configurations..."
if [ -x "$SCRIPT_DIR/install-packages.sh" ]; then
    echo "  -> Injecting binary packages..."
    bash "$SCRIPT_DIR/install-packages.sh"
fi
if [ -x "$SCRIPT_DIR/build-xorg.sh" ]; then
    bash "$SCRIPT_DIR/build-xorg.sh"
fi
if [ -x "$SCRIPT_DIR/build-xfce.sh" ]; then
    bash "$SCRIPT_DIR/build-xfce.sh"
fi
if [ -x "$SCRIPT_DIR/build-calamares.sh" ]; then
    bash "$SCRIPT_DIR/build-calamares.sh"
fi

# ------------------------------------------------------------
# Step 2: Clean and recreate ISO Root Staging Area
# ------------------------------------------------------------
echo "[Step 2] Preparing fresh ISO staging directories..."
rm -rf "$ISO_ROOT"

if [ -d "$IMAGE_DIR" ] && [ ! -w "$IMAGE_DIR" ]; then
    echo "[~] Fixing permissions on $IMAGE_DIR..."
    if [ "$(id -u)" -eq 0 ]; then
        chown -R "${SUDO_USER:-$(id -un)}:${SUDO_USER:-$(id -gn)}" "$IMAGE_DIR" 2>/dev/null || true
    else
        sudo chown -R "$(id -u):$(id -g)" "$IMAGE_DIR" 2>/dev/null || true
    fi
fi

mkdir -p "$IMAGE_DIR"
rm -f "$ISO_OUT"
mkdir -p "$ISO_ROOT/boot/grub/branding"

if [ -f "$KRATOS_ROOT/Branding/KratosOS.png" ]; then
    cp "$KRATOS_ROOT/Branding/KratosOS.png" "$ISO_ROOT/boot/grub/branding/KratosOS.png"
fi

# ------------------------------------------------------------
# Step 3: Copy Kernel Image
# ------------------------------------------------------------
echo "[Step 3] Copying Linux kernel bzImage..."
KERNEL_SRC="$SYSROOT/boot/vmlinuz"
if [ ! -f "$KERNEL_SRC" ]; then
    # Try finding versioned kernel in sysroot/boot/
    KERNEL_SRC=$(find "$SYSROOT/boot" -name "vmlinuz-*" | head -n 1)
fi

if [ -z "$KERNEL_SRC" ] || [ ! -f "$KERNEL_SRC" ]; then
    echo "[!] Error: Kernel image (vmlinuz) not found in $SYSROOT/boot/"
    exit 1
fi

echo "  Kernel: $KERNEL_SRC"
cp "$KERNEL_SRC" "$ISO_ROOT/boot/vmlinuz"
echo "[✓] Kernel copied."

# ------------------------------------------------------------
# Step 3.5: Generate module dependencies
# ------------------------------------------------------------
echo "[Step 3.5] Generating kernel module dependencies (depmod)..."
if [ -d "$SYSROOT/lib/modules" ]; then
    KERNEL_VER=$(ls "$SYSROOT/lib/modules" | head -n 1)
    if [ -n "$KERNEL_VER" ]; then
        echo "  Detected kernel version: $KERNEL_VER"
        # We use the host depmod with the -b (basedir) flag to populate /lib/modules/...
        # in the sysroot with modules.dep, modules.alias, etc.
        depmod -a -b "$SYSROOT" "$KERNEL_VER" || echo "[!] Warning: depmod failed."
    fi
fi

# ------------------------------------------------------------
# Step 4: Package Sysroot into compressed initramfs
# ------------------------------------------------------------
echo "[Step 4] Packing KratosOS sysroot into SquashFS and minimal initramfs..."
INITRAMFS_OUT="$ISO_ROOT/boot/initramfs.cpio.gz"
SQUASHFS_OUT="$ISO_ROOT/live/rootfs.squashfs"

mkdir -p "$ISO_ROOT/live"

# Check for mksquashfs
if ! command -v mksquashfs &>/dev/null; then
    echo "[!] Error: 'mksquashfs' not found. Please install 'squashfs-tools'."
    exit 1
fi

echo "  Creating SquashFS image (this may take a moment)..."
mksquashfs "$SYSROOT" "$SQUASHFS_OUT" -noappend -comp zstd -e boot

echo "  Creating minimal bootstrap initramfs..."
BOOTSTRAP_DIR="$KRATOS_WORK/bootstrap_initramfs"
rm -rf "$BOOTSTRAP_DIR"
# Mirror the sysroot structure: /lib64 -> usr/lib
mkdir -p "$BOOTSTRAP_DIR"/{bin,sbin,etc,usr/lib,lib,dev,proc,sys,mnt,run}
ln -sf usr/lib "$BOOTSTRAP_DIR/lib64"

# Copy essential binaries
cp -v "$SYSROOT/sbin/init" "$BOOTSTRAP_DIR/sbin/init"
cp -v "$SYSROOT/bin/bash" "$BOOTSTRAP_DIR/bin/bash"
ln -sf bash "$BOOTSTRAP_DIR/bin/sh"

# Copy minimal required libraries (ld-linux, libc) to usr/lib
cp -vd "$SYSROOT/usr/lib/"ld-linux* "$BOOTSTRAP_DIR/usr/lib/"
cp -vd "$SYSROOT/usr/lib/"libc.so* "$BOOTSTRAP_DIR/usr/lib/"
cp -vd "$SYSROOT/usr/lib/"libncursesw.so* "$BOOTSTRAP_DIR/usr/lib/"
cp -vd "$SYSROOT/usr/lib/"libm.so* "$BOOTSTRAP_DIR/usr/lib/"
cp -vd "$SYSROOT/usr/lib/"libdl.so* "$BOOTSTRAP_DIR/usr/lib/"
cp -vd "$SYSROOT/usr/lib/"libpthread.so* "$BOOTSTRAP_DIR/usr/lib/"

# Copy essential modules (iso9660, squashfs, overlay, loop, cdrom)
if [ -d "$SYSROOT/lib/modules" ]; then
    KERNEL_VER=$(ls "$SYSROOT/lib/modules" | head -n 1)
    mkdir -p "$BOOTSTRAP_DIR/lib/modules/$KERNEL_VER"
    # Copy kernel modules to /lib/modules (where the kernel expects them)
    cp -r "$SYSROOT/lib/modules/$KERNEL_VER/kernel" "$BOOTSTRAP_DIR/lib/modules/$KERNEL_VER/"
    depmod -a -b "$BOOTSTRAP_DIR" "$KERNEL_VER"
fi

# Copy essential modules (iso9660, squashfs, overlay, loop, cdrom)
if [ -d "$SYSROOT/lib/modules" ]; then
    KERNEL_VER=$(ls "$SYSROOT/lib/modules" | head -n 1)
    mkdir -p "$BOOTSTRAP_DIR/lib/modules/$KERNEL_VER"
    # For now, copy all modules to be safe, they are usually small if not GPU drivers
    # GPU drivers are built-in anyway.
    cp -r "$SYSROOT/lib/modules/$KERNEL_VER/kernel" "$BOOTSTRAP_DIR/lib/modules/$KERNEL_VER/"
    depmod -a -b "$BOOTSTRAP_DIR" "$KERNEL_VER"
fi

(
    cd "$BOOTSTRAP_DIR"
    ln -sf sbin/init init
    find . -print0 | cpio --null -ov --format=newc | gzip -9 > "$INITRAMFS_OUT"
)
rm -rf "$BOOTSTRAP_DIR"

echo "[✓] SquashFS created: $SQUASHFS_OUT"
echo "  Size: $(du -sh "$SQUASHFS_OUT" | cut -f1)"
echo "[✓] Initramfs created: $INITRAMFS_OUT"
echo "  Size: $(du -sh "$INITRAMFS_OUT" | cut -f1)"

# ------------------------------------------------------------
# Step 5: Generate Live GRUB Config
# ------------------------------------------------------------
echo "[Step 5] Generating custom Live CD grub.cfg..."
BUILD_ID="$(date -u +%Y%m%dT%H%M%SZ)"

cat > "$ISO_ROOT/boot/grub/grub.cfg" << GRUB_EOF
# KratosOS Live ISO GRUB Configuration (Parrot OS style)
set timeout=10
set default=0

# Console & Serial output
serial --speed=115200 --unit=0 --word=8 --parity=no --stop=1
terminal_input console serial

# Custom Splash / gfxterm if available
# We load video modules BEFORE setting terminal_output to avoid GRUB hangs.
insmod all_video
insmod gfxterm

if loadfont /boot/grub/fonts/unicode.pf2 ; then
    set gfxmode=auto
    terminal_output gfxterm serial
else
    terminal_output console serial
fi

menuentry "KratosOS Live Session (XFCE)" {
    insmod part_gpt
    insmod fat
    insmod iso9660
    insmod ext2
    insmod linux
    echo "Loading Linux Kernel..."
    linux /boot/vmlinuz rw rdinit=/sbin/init console=ttyS0,115200 console=tty0 loglevel=7 kratos.live
    echo "Loading Live Ramdisk..."
    initrd /boot/initramfs.cpio.gz
    echo "Booting KratosOS Live Environment..."
    boot
}

menuentry "KratosOS Live (Safe Graphics / Nomodeset)" {
    insmod part_gpt
    insmod fat
    insmod iso9660
    insmod ext2
    insmod linux
    echo "Loading Linux Kernel (Safe Graphics)..."
    linux /boot/vmlinuz rw rdinit=/sbin/init console=tty0 console=ttyS0,115200 nomodeset loglevel=3 kratos.live quiet
    echo "Loading Live Ramdisk..."
    initrd /boot/initramfs.cpio.gz
    echo "Booting KratosOS..."
    boot
}

menuentry "KratosOS Live (Debug Mode - Verbose)" {
    insmod part_gpt
    insmod fat
    insmod iso9660
    insmod ext2
    insmod linux
    echo "Loading Linux Kernel (Debug)..."
    linux /boot/vmlinuz rw rdinit=/sbin/init console=tty0 console=ttyS0,115200 loglevel=7 ignore_loglevel earlycon=efifb earlyprintk=efi kratos.live
    echo "Loading Live Ramdisk..."
    initrd /boot/initramfs.cpio.gz
    echo "Booting KratosOS (debug)..."
    boot
}

menuentry "KratosOS Live (Emergency Bash Shell)" {
    insmod part_gpt
    insmod fat
    insmod iso9660
    insmod ext2
    insmod linux
    echo "Loading Linux Kernel (Emergency Shell)..."
    linux /boot/vmlinuz rw rdinit=/bin/bash console=tty0 console=ttyS0,115200 loglevel=7 ignore_loglevel kratos.live
    echo "Loading Live Ramdisk..."
    initrd /boot/initramfs.cpio.gz
    echo "Booting to Bash..."
    boot
}

menuentry "Reboot System" {
    reboot
}

menuentry "Power Off System" {
    halt
}
GRUB_EOF

echo "[✓] Live grub.cfg written."

# ------------------------------------------------------------
# Step 6: Build ISO via grub-mkrescue
# ------------------------------------------------------------
echo "[Step 6] Invoking grub-mkrescue..."
grub-mkrescue -o "$ISO_OUT" "$ISO_ROOT" 2>&1 | sed 's/^/    /'

# ------------------------------------------------------------
# Step 7: Cleanup
# ------------------------------------------------------------
echo "[Step 7] Cleaning up staging directory..."
rm -rf "$ISO_ROOT"

echo
echo "========================================"
echo "    [✓] ISO IMAGE CREATION COMPLETE"
echo "========================================"
echo "  ISO:   $ISO_OUT"
echo "  Size:  $(du -sh "$ISO_OUT" | cut -f1)"
echo
echo "To test in QEMU:"
echo "  qemu-system-x86_64 -m 1G -cdrom $ISO_OUT -boot d"
echo "  (For UEFI boot, add OVMF parameters like in run-qemu.sh)"
echo
