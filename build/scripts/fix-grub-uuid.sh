#!/usr/bin/env bash
# fix-grub-uuid.sh — Fix UUID mismatch in grub.cfg inside kratosos.img
# Usage: sudo ./fix-grub-uuid.sh

set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "[!] Run with sudo."
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"

IMAGE="$KRATOS_ROOT/build/images/kratosos.img"

echo "[+] Image: $IMAGE"

# Attach loop device
LOOP="$(losetup --find --show --partscan "$IMAGE")"
echo "[+] Loop: $LOOP"

ROOT_DEV="${LOOP}p2"

# Wait for device
sleep 1

# Get real UUID
REAL_UUID="$(blkid -s UUID -o value "$ROOT_DEV")"
echo "[+] Real UUID of root (p2): $REAL_UUID"

MNT="$(mktemp -d)"

cleanup() {
    set +e
    mountpoint -q "$MNT" && umount "$MNT"
    [ -d "$MNT" ] && rmdir "$MNT"
    losetup -d "$LOOP" 2>/dev/null || true
}
trap cleanup EXIT

mount "$ROOT_DEV" "$MNT"

echo
echo "=== Current grub.cfg ==="
cat "$MNT/boot/grub/grub.cfg"

# Write correct grub.cfg
cat > "$MNT/boot/grub/grub.cfg" << GRUB_EOF
# KratosOS GRUB configuration
# Root UUID: ${REAL_UUID}
set timeout=3
set default=0

menuentry "KratosOS 0.1.0" {
    insmod part_gpt
    insmod ext2
    insmod linux

    search --no-floppy --fs-uuid --set=root ${REAL_UUID}

    linux /boot/vmlinuz root=UUID=${REAL_UUID} rw console=ttyS0 console=tty0
}
GRUB_EOF

echo
echo "=== New grub.cfg ==="
cat "$MNT/boot/grub/grub.cfg"

sync

echo
echo "[✓] Done. UUID applied: $REAL_UUID"
