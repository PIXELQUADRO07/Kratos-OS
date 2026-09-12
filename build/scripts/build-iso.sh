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

GRUB_MKRESCUE_BIN="$(command -v grub-mkrescue 2>/dev/null || command -v grub2-mkrescue 2>/dev/null || true)"
if [ -z "$GRUB_MKRESCUE_BIN" ] || ! command -v xorriso &>/dev/null; then
    echo "[!] Error: 'grub-mkrescue' (or 'grub2-mkrescue') or 'xorriso' utility not found."
    echo "    Install missing host dependencies via:"
    echo "      Arch Linux:    sudo pacman -S --needed grub xorriso"
    echo "      Debian/Ubuntu: sudo apt install grub-common xorriso mtools"
    echo "      Fedora:        sudo dnf install grub2-tools-extra xorriso mtools"
    exit 1
fi

# ------------------------------------------------------------
# Step 1: Sync latest Live, Desktop and Calamares configurations
# ------------------------------------------------------------
echo "[Step 1] Checking Live, Desktop and Calamares configurations..."
if [ ! -f "$KRATOS_ROOT/build/.stamps/inject-pkgs.done" ] && [ -x "$SCRIPT_DIR/install-packages.sh" ]; then
    echo "  -> Injecting binary packages..."
    bash "$SCRIPT_DIR/install-packages.sh"
fi
if [ ! -f "$KRATOS_ROOT/build/.stamps/xorg.done" ] && [ -x "$SCRIPT_DIR/build-xorg.sh" ]; then
    bash "$SCRIPT_DIR/build-xorg.sh"
fi
if [ ! -f "$KRATOS_ROOT/build/.stamps/xfce.done" ] && [ -x "$SCRIPT_DIR/build-xfce.sh" ]; then
    bash "$SCRIPT_DIR/build-xfce.sh"
fi
if [ ! -f "$KRATOS_ROOT/build/.stamps/calamares.done" ] && [ -x "$SCRIPT_DIR/build-calamares.sh" ]; then
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
    fi
fi

mkdir -p "$IMAGE_DIR"
rm -f "$ISO_OUT"
mkdir -p "$ISO_ROOT/boot/grub/branding"
mkdir -p "$ISO_ROOT/boot/grub/themes"

if [ -d "$KRATOS_ROOT/config/grub/themes/kratosos" ]; then
    cp -r "$KRATOS_ROOT/config/grub/themes/kratosos" "$ISO_ROOT/boot/grub/themes/"
fi

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

# Ensure correct permissions for critical suid binaries and runtime dirs
echo "  Sanitizing sysroot permissions and runtime directories..."
for suid_bin in "$SYSROOT/usr/bin/sudo" "$SYSROOT/usr/bin/passwd" "$SYSROOT/bin/su" "$SYSROOT/usr/bin/pkexec" "$SYSROOT/usr/bin/Xorg" "$SYSROOT/usr/lib/polkit-1/polkit-agent-helper-1"; do
    if [ -f "$suid_bin" ]; then
        chmod 4755 "$suid_bin" 2>/dev/null || true
    fi
done
if [ -f "$SYSROOT/etc/live/start-live.sh" ]; then
    chmod +x "$SYSROOT/etc/live/start-live.sh" 2>/dev/null || true
fi
if [ -d "$SYSROOT/etc/rc.d" ]; then
    chmod +x "$SYSROOT/etc/rc.d"/* 2>/dev/null || true
fi
if [ -f "$SYSROOT/etc/rc.sysinit" ]; then
    chmod +x "$SYSROOT/etc/rc.sysinit" 2>/dev/null || true
fi
chmod 1777 "$SYSROOT/tmp" 2>/dev/null || true
mkdir -p "$SYSROOT/tmp/.ICE-unix" "$SYSROOT/tmp/.X11-unix"
chmod 1777 "$SYSROOT/tmp/.ICE-unix" "$SYSROOT/tmp/.X11-unix" 2>/dev/null || true
chmod 777 "$SYSROOT/var/log" "$SYSROOT/var/lib/xkb" 2>/dev/null || true

echo "  Creating SquashFS image (this may take a moment)..."
mksquashfs "$SYSROOT" "$SQUASHFS_OUT" -noappend -all-root -comp zstd -e boot

echo "  Creating minimal bootstrap initramfs..."
BOOTSTRAP_DIR="$KRATOS_WORK/bootstrap_initramfs"
rm -rf "$BOOTSTRAP_DIR"
mkdir -p "$BOOTSTRAP_DIR"/{bin,sbin,dev,proc,sys,run,mnt/media,mnt/rofs,mnt/cow,mnt/newroot}

CC="$KRATOS_TOOLS/bin/$TARGET-gcc"
STRIP="$KRATOS_TOOLS/bin/$TARGET-strip"

echo "  Compiling static live /init binary..."
"$CC" --sysroot="$SYSROOT" -static -O2 -Wall -Wextra -std=gnu11 \
    -o "$BOOTSTRAP_DIR/init" "$KRATOS_ROOT/init/live.c"
"$STRIP" "$BOOTSTRAP_DIR/init"

# Copy bash as emergency shell
if [ -f "$SYSROOT/bin/bash" ]; then
    cp -v "$SYSROOT/bin/bash" "$BOOTSTRAP_DIR/bin/bash"
    ln -sf bash "$BOOTSTRAP_DIR/bin/sh"
fi

# Copy real ELF DT_NEEDED libraries (skip GNU ld scripts like libdl.so).
copy_so_file() {
    local src="$1"
    local dest_dir="$2"
    [ -e "$src" ] || return 0
    if [ -f "$src" ] && ! [ -L "$src" ] && grep -q "GNU ld script" "$src" 2>/dev/null; then
        return 0
    fi
    mkdir -p "$dest_dir"
    cp -a "$src" "$dest_dir/"
    if [ -L "$src" ]; then
        local tgt
        tgt="$(readlink -f "$src" || true)"
        if [ -n "$tgt" ] && [ -e "$tgt" ] && [ "$tgt" != "$src" ]; then
            cp -a "$tgt" "$dest_dir/"
        fi
    fi
}

DEPS_SEEN="$BOOTSTRAP_DIR/.deps_seen"
: > "$DEPS_SEEN"

copy_needed_libs() {
    local bin="$1"
    local dest="$BOOTSTRAP_DIR/usr/lib"
    mkdir -p "$dest"

    local interp
    interp="$(readelf -l "$bin" 2>/dev/null | sed -n 's/.*Requesting program interpreter: \(.*\)]/\1/p' || true)"
    if [ -n "$interp" ]; then
        local ipath="$SYSROOT$interp"
        if [ ! -e "$ipath" ]; then
            ipath="$(find "$SYSROOT/usr/lib" "$SYSROOT/lib" "$SYSROOT/lib64" "$SYSROOT/usr/lib64" \
                -name "$(basename "$interp")" 2>/dev/null | head -n 1 || true)"
        fi
        if [ -n "$ipath" ] && [ -e "$ipath" ]; then
            copy_so_file "$ipath" "$dest"
            mkdir -p "$BOOTSTRAP_DIR$(dirname "$interp")"
            if [ ! -e "$BOOTSTRAP_DIR$interp" ]; then
                ln -sf "/usr/lib/$(basename "$(readlink -f "$ipath")")" "$BOOTSTRAP_DIR$interp" 2>/dev/null || \
                    cp -a "$ipath" "$BOOTSTRAP_DIR$interp"
            fi
        fi
    fi

    local lib found real
    while read -r lib; do
        [ -n "$lib" ] || continue
        grep -qxF "$lib" "$DEPS_SEEN" && continue
        echo "$lib" >> "$DEPS_SEEN"
        found="$(find "$SYSROOT/usr/lib" "$SYSROOT/lib" "$SYSROOT/usr/lib64" "$SYSROOT/lib64" \
            -maxdepth 2 -name "$lib" 2>/dev/null | head -n 1 || true)"
        if [ -z "$found" ]; then
            echo "    [!] missing shared library $lib for $(basename "$bin")"
            continue
        fi
        copy_so_file "$found" "$dest"
        real="$(readlink -f "$found" || true)"
        if [ -n "$real" ] && [ -f "$real" ]; then
            copy_needed_libs "$real"
        fi
    done < <(readelf -d "$bin" 2>/dev/null | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' || true)
}

if [ -f "$BOOTSTRAP_DIR/bin/bash" ]; then
    copy_needed_libs "$BOOTSTRAP_DIR/bin/bash"
    # Ensure lib64 symlink if dynamic loader is under /lib64
    if [ -d "$BOOTSTRAP_DIR/usr/lib" ] && [ ! -e "$BOOTSTRAP_DIR/lib64" ]; then
        ln -sf usr/lib "$BOOTSTRAP_DIR/lib64"
    fi
fi
rm -f "$DEPS_SEEN"

# Copy only live-boot modules (if built as modules). Skip the full tree.
copy_kmod() {
    local kver="$1"
    local name="$2"
    local src_root="$SYSROOT/lib/modules/$kver"
    local dst_root="$BOOTSTRAP_DIR/lib/modules/$kver"
    [ -d "$src_root" ] || return 0
    local f
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        local rel="${f#"$src_root"/}"
        mkdir -p "$dst_root/$(dirname "$rel")"
        cp -a "$f" "$dst_root/$rel"
    done < <(find "$src_root" -type f \( -name "${name}.ko" -o -name "${name}.ko.*" \) 2>/dev/null || true)
}

if [ -d "$SYSROOT/lib/modules" ]; then
    KERNEL_VER=$(ls "$SYSROOT/lib/modules" | head -n 1)
    if [ -n "$KERNEL_VER" ]; then
        mkdir -p "$BOOTSTRAP_DIR/lib/modules/$KERNEL_VER"

        # ── Storage / ISO / overlay (boot-critical, always first) ────────────
        for _kmod in loop isofs squashfs overlay cdrom sr_mod sd_mod usb-storage \
            virtio virtio_pci virtio_blk virtio_scsi virtio_ring ahci libahci ata_piix; do
            copy_kmod "$KERNEL_VER" "$_kmod"
        done

        # ── DRM subsystem core (required by every DRM driver below) ──────────
        # drm and drm_kms_helper must be loaded before any GPU driver.
        for _kmod in drm drm_kms_helper drm_display_helper drm_buddy drm_exec \
            video button backlight; do
            copy_kmod "$KERNEL_VER" "$_kmod"
        done

        # ── Real-hardware GPU drivers ────────────────────────────────────────
        # These are compiled as modules (--module in build-kernel.sh) so they
        # land in the squashfs only — which the kernel cannot access before
        # Xorg initialises the display.  Copy them into the initramfs so they
        # are available at first-boot, on any real machine.
        #   i915 / intel_gtt / intel_agp  → Intel iGPU Gen4–Xe
        #   amdgpu / amdttm / amdkcl      → AMD GCN/RDNA
        #   radeon                         → AMD pre-GCN / legacy
        #   nouveau / nvkm                 → Nvidia (open firmware path)
        #   ttm                            → TTM memory manager (amdgpu/nouveau dep)
        #   ast / mgag200                  → server BMC / Matrox
        #   vmwgfx                         → VMware SVGA
        echo "  Copying real-hardware DRM drivers into initramfs..."
        for _kmod in \
            i915 intel_gtt intel_agp \
            amdgpu amd_iommu_v2 amdttm amdkcl \
            radeon \
            nouveau nvkm \
            ttm \
            drm_ast ast \
            drm_mgag200 mgag200 \
            drm_vmwgfx vmwgfx; do
            copy_kmod "$KERNEL_VER" "$_kmod"
        done

        for _meta in modules.order modules.builtin modules.builtin.modinfo; do
            if [ -e "$SYSROOT/lib/modules/$KERNEL_VER/$_meta" ]; then
                cp -a "$SYSROOT/lib/modules/$KERNEL_VER/$_meta" \
                    "$BOOTSTRAP_DIR/lib/modules/$KERNEL_VER/"
            fi
        done
        depmod -a -b "$BOOTSTRAP_DIR" "$KERNEL_VER" || echo "[!] Warning: depmod failed."
    fi
fi

# ── GPU firmware blobs ───────────────────────────────────────────────────────
# Firmware is already downloaded/extracted by build-firmware.sh into
# $SYSROOT/lib/firmware.  Copy the GPU subdirs into the bootstrap initramfs
# so the kernel can upload microcode during driver probe (before squashfs mount).
echo "  Copying GPU firmware blobs into initramfs..."
BOOT_FW_DIR="$BOOTSTRAP_DIR/lib/firmware"
SYSROOT_FW="$SYSROOT/lib/firmware"
mkdir -p "$BOOT_FW_DIR"
for _fw_dir in i915 amdgpu radeon nouveau; do
    if [ -d "$SYSROOT_FW/$_fw_dir" ]; then
        echo "    - $_fw_dir firmware..."
        mkdir -p "$BOOT_FW_DIR/$_fw_dir"
        cp -r "$SYSROOT_FW/$_fw_dir/." "$BOOT_FW_DIR/$_fw_dir/"
    else
        echo "    [~] $_fw_dir firmware not found in sysroot — skipping"
    fi
done

(
    cd "$BOOTSTRAP_DIR"
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

# Custom Splash / gfxterm / Theme if available
# We load video modules BEFORE setting terminal_output to avoid GRUB hangs.
insmod all_video
insmod gfxterm
insmod png

if [ -f /boot/grub/themes/kratosos/theme.txt ]; then
    set theme=/boot/grub/themes/kratosos/theme.txt
    export theme
    set gfxmode=1920x1080,auto
    terminal_output gfxterm serial
elif loadfont /boot/grub/fonts/unicode.pf2 ; then
    set gfxmode=auto
    terminal_output gfxterm serial
else
    terminal_output console serial
fi

set color_normal=light-gray/black
set color_highlight=black/red

menuentry "KratosOS Live Session (XFCE)" {
    insmod part_gpt
    insmod fat
    insmod iso9660
    insmod ext2
    insmod linux
    echo "Loading Linux Kernel..."
    linux /boot/vmlinuz rw rdinit=/init console=tty0 console=ttyS0,115200 loglevel=7 kratos.live
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
    linux /boot/vmlinuz rw rdinit=/init console=tty0 console=ttyS0,115200 nomodeset loglevel=3 kratos.live quiet
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
    linux /boot/vmlinuz rw rdinit=/init console=tty0 console=ttyS0,115200 loglevel=7 ignore_loglevel earlycon=efifb earlyprintk=efi kratos.live
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
echo "[Step 6] Invoking grub-mkrescue ($GRUB_MKRESCUE_BIN)..."
"$GRUB_MKRESCUE_BIN" -o "$ISO_OUT" "$ISO_ROOT" 2>&1 | sed 's/^/    /'

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
