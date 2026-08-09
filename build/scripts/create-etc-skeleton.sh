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

echo "[+] Creating /etc/passwd..."
cat > "$ETC/passwd" <<'EOF'
root:x:0:0:root:/root:/bin/bash
EOF

echo "[+] Creating /etc/group..."
cat > "$ETC/group" <<'EOF'
root:x:0:
tty:x:5:
EOF

echo "[+] Creating /etc/shadow..."
cat > "$ETC/shadow" <<'EOF'
root:*:19700:0:99999:7:::
EOF
chmod 600 "$ETC/shadow"

echo "[+] Creating /etc/shells..."
cat > "$ETC/shells" <<'EOF'
/bin/sh
/bin/bash
EOF

echo "[+] Creating /etc/hostname..."
cat > "$ETC/hostname" <<'EOF'
kratos
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
VERSION="0.1.0"
VERSION_ID="0.1.0"
PRETTY_NAME="KratosOS 0.1.0"
HOME_URL="https://kratosos.org"
EOF

echo "[+] Creating /etc/issue..."
cat > "$ETC/issue" <<'EOF'

  ██╗  ██╗██████╗  █████╗ ████████╗ ██████╗ ███████╗
  ██║ ██╔╝██╔══██╗██╔══██╗╚══██╔══╝██╔═══██╗██╔════╝
  █████╔╝ ██████╔╝███████║   ██║   ██║   ██║███████╗
  ██╔═██╗ ██╔══██╗██╔══██║   ██║   ██║   ██║╚════██║
  ██║  ██╗██║  ██║██║  ██║   ██║   ╚██████╔╝███████╗
  ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝   ╚═╝    ╚═════╝ ╚══════╝

  KratosOS 0.1.0 (GNU/Linux \r)
  Kernel \v on \m (\l)

EOF

echo "[+] Creating /etc/profile..."
cat > "$ETC/profile" <<'EOF'
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export HOME=/root
export SHELL=/bin/bash
export TERM=linux
export PS1='\[\033[1;32m\]kratOS\[\033[0m\]:\[\033[1;34m\]\w\[\033[0m\]# '
EOF

echo "[+] Creating /etc/rc.sysinit..."
cat > "$ETC/rc.sysinit" <<'EOF'
#!/bin/bash
# /etc/rc.sysinit — KratosOS Early System Initialization

echo "[rc.sysinit] Starting KratosOS initialization..."

# Set up loopback network interface
if command -v ip >/dev/null 2>&1; then
    ip link set dev lo up 2>/dev/null || true
elif command -v ifconfig >/dev/null 2>&1; then
    ifconfig lo 127.0.0.1 up 2>/dev/null || true
fi

# Ensure /etc/mtab points to /proc/self/mounts
if [ ! -L /etc/mtab ]; then
    ln -sf /proc/self/mounts /etc/mtab 2>/dev/null || true
fi

echo "[rc.sysinit] System initialization complete."
EOF
chmod +x "$ETC/rc.sysinit"

echo
echo "[+] /etc skeleton created successfully."
echo
echo "Files:"
find "$ETC" -maxdepth 2 -printf "  %P\n" | sort
