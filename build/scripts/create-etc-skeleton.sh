#!/usr/bin/env bash

# create-etc-skeleton.sh — Populate KratosOS /etc skeleton

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SYSROOT="$PROJECT_ROOT/build/sysroot"
ETC="$SYSROOT/etc"

echo "================================"
echo "   KRATOSOS /etc SKELETON"
echo "================================"
echo

mkdir -p "$ETC"
mkdir -p "$ETC/rc.d"
mkdir -p "$ETC/network"

# ---------------------------------------------------------------------------
# FHS base directories — these are pure MOUNT POINTS, not populated by any
# package build, so nothing else in the pipeline ever creates them. Their
# absence is silent at build time (nothing fails) but fatal at boot time:
# the kernel's own CONFIG_DEVTMPFS_MOUNT auto-mount of devtmpfs onto /dev
# right after pivoting into the real root fails with ENOENT if /dev isn't
# there, and every mount() call in init's mount_vfs() (/proc, /sys, /dev,
# /dev/pts, /dev/shm) fails the same way — the system limps on with no
# real device nodes and no way to open a tty, which is exactly the
# "hangs after network init, never reaches a shell" symptom.
echo "[+] Creating FHS base/mountpoint directories..."
for d in proc sys dev dev/pts dev/shm run tmp mnt media opt srv home root boot; do
    mkdir -p "$SYSROOT/$d"
done
chmod 1777 "$SYSROOT/tmp"      # sticky bit: shared, world-writable, no cross-user delete
chmod 0700 "$SYSROOT/root"     # root's home: root-only

# NOTE: kratos-devd is intentionally NOT launched from /etc/rc.d/. init.c's
# start_devd() already starts it early in the boot sequence (before
# mount_fstab(), since UUID/LABEL resolution depends on it) and blocks until
# its coldplug scan completes. An rc.d entry used to duplicate this, causing
# a second devd instance to double-process every uevent and re-run the
# by-uuid/by-label symlink logic concurrently with the first. Don't re-add it.

echo "[+] Creating /etc/passwd..."
cat > "$ETC/passwd" <<'EOF'
root:x:0:0:root:/root:/bin/bash
EOF

echo "[+] Creating /etc/group..."
cat > "$ETC/group" <<'EOF'
root:x:0:
tty:x:5:
disk:x:6:
EOF

echo "[+] Creating /etc/shadow..."
# NOTE: login.c treats a "*" or "!" hash as a LOCKED account and skips the
# password prompt entirely — which in this custom login flow actually means
# "let anyone in without a password" (the opposite of "*"'s usual meaning on
# other systems). We use the standard empty-password field instead: it hits
# the same "skip prompt" branch in login.c (hash length 0) but is the
# conventional way to say "no password set", so it won't confuse anyone
# relying on normal shadow(5) semantics later.
cat > "$ETC/shadow" <<'EOF'
root::19700:0:99999:7:::
EOF
chmod 600 "$ETC/shadow"

echo "[+] Creating /etc/nsswitch.conf..."
cat > "$ETC/nsswitch.conf" <<'EOF'
# KratosOS NSS configuration
passwd:    files
group:     files
shadow:    files
hosts:     files dns
networks:  files
EOF

echo "[+] Creating /etc/shells..."
cat > "$ETC/shells" <<'EOF'
/bin/sh
/bin/bash
EOF

echo "[+] Creating /etc/hostname..."
cat > "$ETC/hostname" <<'EOF'
kratos-os
EOF

echo "[+] Creating /etc/fstab..."
cat > "$ETC/fstab" <<'EOF'
# KratosOS filesystem table
# <file system> <mount point>   <type>      <options>                   <dump>  <pass>
proc            /proc           proc        defaults                    0       0
sysfs           /sys            sysfs       defaults                    0       0
devtmpfs        /dev            devtmpfs    mode=0755,nosuid            0       0
devpts          /dev/pts        devpts      gid=5,mode=620              0       0
tmpfs           /run            tmpfs       mode=0755,nosuid,nodev      0       0
tmpfs           /tmp            tmpfs       mode=1777,nosuid,nodev      0       0
EOF

echo "[+] Creating /etc/os-release..."
cat > "$ETC/os-release" <<'EOF'
NAME="KratosOS"
ID=kratos
VERSION="0.7.8.2"
VERSION_ID="0.7.8.2"
PRETTY_NAME="KratosOS 0.7.8.2"
HOME_URL="https://kratosos.org"
EOF

echo "[+] Creating /etc/issue..."
cat > "$ETC/issue" <<'EOF'

.--..--..--..--..--..--..--..--..--..--..--..--..--..--..--..--..--..--..--..
|                                                                            |
| ██╗  ██╗██████╗  █████╗ ████████╗ ██████╗ ███████╗       ██████╗ ███████╗  |
| ██║ ██╔╝██╔══██╗██╔══██╗╚══██╔══╝██╔═══██╗██╔════╝      ██╔═══██╗██╔════╝  |
| █████╔╝ ██████╔╝███████║   ██║   ██║   ██║███████╗█████╗██║   ██║███████╗  |
| ██╔═██╗ ██╔══██╗██╔══██║   ██║   ██║   ██║╚════██║╚════╝██║   ██║╚════██║  |
| ██║  ██╗██║  ██║██║  ██║   ██║   ╚██████╔╝███████║      ╚██████╔╝███████║  |
| ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝   ╚═╝    ╚═════╝ ╚══════╝       ╚═════╝ ╚══════╝  |
|                                                                            |
.--..--..--..--..--..--..--..--..--..--..--..--..--..--..--..--..--..--..--..

  KratosOS 0.7.8.2 (GNU/Linux \r)
  Kernel \v on \m (\l)

EOF

echo "[+] Creating /etc/profile..."
cat > "$ETC/profile" <<'EOF'
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export HOME=/root
export SHELL=/bin/bash
export TERM=linux
export PS1='\[\033[1;32m\]\h\[\033[0m\]:\[\033[1;34m\]\w\[\033[0m\]# '
EOF

echo "[+] Creating /etc/resolv.conf..."
cat > "$ETC/resolv.conf" <<'EOF'
# KratosOS DNS Resolv Configuration
nameserver 1.1.1.1
nameserver 8.8.8.8
EOF

echo "[+] Creating /etc/network/interfaces..."
cat > "$ETC/network/interfaces" <<'EOF'
# KratosOS network configuration

auto lo
iface lo inet loopback

auto eth0
iface eth0 inet dhcp
EOF

echo "[+] Pre-creating /etc/mtab symlink..."
ln -sf /proc/self/mounts "$ETC/mtab"

echo "[+] Creating /etc/rc.sysinit..."
cat > "$ETC/rc.sysinit" <<'EOF'
#!/bin/bash
# /etc/rc.sysinit — KratosOS Early System Initialization

echo "[rc.sysinit] Starting KratosOS initialization..."

# Ensure /etc/mtab points to /proc/self/mounts
if [ ! -L /etc/mtab ]; then
    ln -sf /proc/self/mounts /etc/mtab 2>/dev/null || true
fi

echo "[rc.sysinit] System initialization complete."
EOF
chmod +x "$ETC/rc.sysinit"

echo "[+] Creating /etc/rc.d/10-network..."
cat > "$ETC/rc.d/10-network" <<'EOF'
#!/bin/bash
# /etc/rc.d/10-network — Launch KratosOS Network Manager

if [ -x /sbin/kratos-net ]; then
    echo "[rc.d] Initializing network via kratos-net..."
    /sbin/kratos-net --auto
fi
EOF
chmod +x "$ETC/rc.d/10-network"

echo
echo "[+] /etc skeleton created successfully."
echo
echo "Files:"
find "$ETC" -maxdepth 2 -printf "  %P\n" | sort
