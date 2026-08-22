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
mkdir -p "$ETC/profile.d"

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
for d in proc sys dev dev/pts dev/shm run tmp mnt media opt srv home root home/kratos-live boot etc/ssl/certs etc/sudoers.d; do
    mkdir -p "$SYSROOT/$d"
done
chmod 1777 "$SYSROOT/tmp"      # sticky bit: shared, world-writable, no cross-user delete
chmod 0700 "$SYSROOT/root"     # root's home: root-only
chmod 0755 "$SYSROOT/home/kratos-live"
chown 1000:1000 "$SYSROOT/home/kratos-live" 2>/dev/null || true

# NOTE: kratos-devd is intentionally NOT launched from /etc/rc.d/. init.c's
# start_devd() already starts it early in the boot sequence (before
# mount_fstab(), since UUID/LABEL resolution depends on it) and blocks until
# its coldplug scan completes. An rc.d entry used to duplicate this, causing
# a second devd instance to double-process every uevent and re-run the
# by-uuid/by-label symlink logic concurrently with the first. Don't re-add it.

echo "[+] Creating /etc/passwd..."
cat > "$ETC/passwd" <<'EOF'
root:x:0:0:root:/root:/bin/bash
bin:x:1:1:bin:/dev/null:/usr/bin/false
daemon:x:6:6:Daemon User:/dev/null:/usr/bin/false
messagebus:x:18:18:D-Bus Message Daemon User:/run/dbus:/usr/bin/false
systemd-journal-gateway:x:73:73:systemd Journal Gateway:/:/usr/bin/false
systemd-journal-remote:x:74:74:systemd Journal Remote:/:/usr/bin/false
systemd-journal-upload:x:75:75:systemd Journal Upload:/:/usr/bin/false
systemd-network:x:76:76:systemd Network Management:/:/usr/bin/false
systemd-resolve:x:77:77:systemd Resolver:/:/usr/bin/false
systemd-timesync:x:78:78:systemd Time Synchronization:/:/usr/bin/false
systemd-coredump:x:79:79:systemd Core Dumper:/:/usr/bin/false
nobody:x:65534:65534:Unprivileged User:/dev/null:/usr/bin/false
kratos-live:x:1000:1000:KratosOS Live User:/home/kratos-live:/bin/bash
EOF

echo "[+] Creating /etc/group..."
cat > "$ETC/group" <<'EOF'
root:x:0:
bin:x:1:daemon
sys:x:2:
kmem:x:3:
tape:x:4:
tty:x:5:kratos-live
daemon:x:6:
floppy:x:7:
disk:x:8:
lp:x:9:
dialout:x:10:
audio:x:11:root,kratos-live
video:x:12:root,kratos-live
utmp:x:13:
usb:x:14:
cdrom:x:15:
adm:x:16:
messagebus:x:18:
systemd-journal:x:23:
input:x:24:root,kratos-live
mail:x:34:
kvm:x:61:
systemd-journal-gateway:x:73:
systemd-journal-remote:x:74:
systemd-journal-upload:x:75:
systemd-network:x:76:
systemd-resolve:x:77:
systemd-timesync:x:78:
systemd-coredump:x:79:
wheel:x:97:root,kratos-live
users:x:999:kratos-live
kratos-live:x:1000:
nogroup:x:65534:
EOF

echo "[+] Creating /etc/shadow..."
cat > "$ETC/shadow" <<'EOF'
root::19700:0:99999:7:::
kratos-live::19700:0:99999:7:::
EOF
chmod 600 "$ETC/shadow"

echo "[+] Creating /etc/hosts..."
cat > "$ETC/hosts" <<'EOF'
127.0.0.1   localhost kratos-os
::1         localhost kratos-os
EOF

echo "[+] Creating /etc/nsswitch.conf..."
cat > "$ETC/nsswitch.conf" <<'EOF'
# KratosOS NSS configuration
passwd:    files
group:     files
shadow:    files
hosts:     files dns
networks:  files
protocols: files
services:  files
ethers:    files
rpc:       files
EOF

echo "[+] Creating /etc/protocols..."
cat > "$ETC/protocols" <<'EOF'
ip      0       IP
icmp    1       ICMP
igmp    2       IGMP
ggp     3       GGP
tcp     6       TCP
pup     12      PUP
udp     17      UDP
idp     22      IDP
raw     255     RAW
EOF

echo "[+] Creating /etc/services..."
cat > "$ETC/services" <<'EOF'
ssh             22/tcp
domain          53/tcp
domain          53/udp
http            80/tcp
https           443/tcp
EOF

echo "[+] Creating /etc/localtime..."
ln -sf /usr/share/zoneinfo/UTC "$ETC/localtime"

echo "[+] Creating /etc/ld.so.conf..."
cat > "$ETC/ld.so.conf" <<'EOF'
/usr/local/lib
/opt/lib
include /etc/ld.so.conf.d/*.conf
EOF

echo "[+] Creating /etc/shells..."
cat > "$ETC/shells" <<'EOF'
/bin/sh
/bin/bash
EOF

echo "[+] Creating /etc/sudoers..."
cat > "$ETC/sudoers" <<'EOF'
root ALL=(ALL:ALL) ALL
%wheel ALL=(ALL:ALL) NOPASSWD: ALL
kratos-live ALL=(ALL:ALL) NOPASSWD: ALL
#includedir /etc/sudoers.d
EOF
chmod 440 "$ETC/sudoers"

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

echo "[+] Creating /etc/profile (LFS 13.0 style)..."
cat > "$ETC/profile" <<'EOF'
# /etc/profile — KratosOS Global Environment Initialization

# Functions to help managing paths
pathremove () {
        local IFS=':'
        local NEWPATH
        local DIR
        local PATHVARIABLE=${2:-PATH}
        for DIR in ${!PATHVARIABLE} ; do
                if [ "$DIR" != "$1" ] ; then
                        NEWPATH=${NEWPATH:+$NEWPATH:}$DIR
                fi
        done
        export $PATHVARIABLE="$NEWPATH"
}

pathprepend () {
        pathremove $1 $2
        local PATHVARIABLE=${2:-PATH}
        export $PATHVARIABLE="$1${!PATHVARIABLE:+:${!PATHVARIABLE}}"
}

pathappend () {
        pathremove $1 $2
        local PATHVARIABLE=${2:-PATH}
        export $PATHVARIABLE="${!PATHVARIABLE:+${!PATHVARIABLE}:}$1"
}

# Set a basic PATH
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

# Default environment
export HOME=${HOME:-/root}
export SHELL=${SHELL:-/bin/bash}
export TERM=${TERM:-linux}

# Source modular profile scripts
if [ -d /etc/profile.d ]; then
  for i in /etc/profile.d/*.sh; do
    if [ -r "$i" ]; then
      . "$i"
    fi
  done
  unset i
fi

# Source bashrc for interactive shells
if [ -n "$BASH_VERSION" ] && [ -f /etc/bash.bashrc ]; then
  . /etc/bash.bashrc
fi
EOF

echo "[+] Creating /etc/bash.bashrc..."
cat > "$ETC/bash.bashrc" <<'EOF'
# /etc/bash.bashrc — KratosOS System-wide Bash Configuration

# PS1: [user@host:cwd]#
export PS1='\[\033[1;32m\]\u@\h\[\033[0m\]:\[\033[1;34m\]\w\[\033[0m\]\$ '

# Useful aliases
alias ls='ls --color=auto'
alias ll='ls -l'
alias l='ls -CF'
alias grep='grep --color=auto'
EOF

echo "[+] Creating /etc/profile.d/umask.sh..."
cat > "$ETC/profile.d/umask.sh" <<'EOF'
# By default we want the umask to get set.
if [ "$(id -gn)" = "$(id -un)" -a $EUID -gt 99 ]; then
  umask 002
else
  umask 022
fi
EOF

echo "[+] Creating /etc/profile.d/i18n.sh..."
cat > "$ETC/profile.d/i18n.sh" <<'EOF'
# Set up i18n variables
export LANG=en_US.UTF-8
EOF

echo "[+] Creating /etc/profile.d/dircolors.sh..."
cat > "$ETC/profile.d/dircolors.sh" <<'EOF'
# Set up dircolors
if [ -x /usr/bin/dircolors ]; then
    if [ -f /etc/dircolors ]; then
        eval $(dircolors -b /etc/dircolors)
    else
        eval $(dircolors -b)
    fi
fi
EOF

echo "[+] Creating /etc/profile.d/readline.sh..."
cat > "$ETC/profile.d/readline.sh" <<'EOF'
# Set up INPUTRC
if [ -z "$INPUTRC" -a ! -f "$HOME/.inputrc" ]; then
  export INPUTRC=/etc/inputrc
fi
EOF

echo "[+] Creating /etc/profile.d/extrapaths.sh..."
cat > "$ETC/profile.d/extrapaths.sh" <<'EOF'
# Setup extra paths
if [ -d /usr/local/lib/pkgconfig ]; then
  pathappend /usr/local/lib/pkgconfig PKG_CONFIG_PATH
fi
if [ -d /usr/lib/pkgconfig ]; then
  pathappend /usr/lib/pkgconfig PKG_CONFIG_PATH
fi
EOF

echo "[+] Creating /etc/inputrc..."
cat > "$ETC/inputrc" <<'EOF'
# /etc/inputrc — LFS 13.0 standard inputrc
set horizontal-scroll-mode Off
set meta-flag On
set input-meta On
set output-meta On
set show-all-if-ambiguous On
set bell-style none

# Arrow keys history search
"\e[A": history-search-backward
"\e[B": history-search-forward
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

echo "[+] Creating Kratos Package Manager directories..."
mkdir -p "$ETC/kratos/repos.d"
mkdir -p "$ETC/kratos/keys"
mkdir -p "$SYSROOT/var/lib/kratos/repo-cache"

if [ -f "$PROJECT_ROOT/config/keys/official.pub" ]; then
    echo "[+] Embedding official repository public key..."
    cp "$PROJECT_ROOT/config/keys/official.pub" "$ETC/kratos/keys/official.pub"
fi

echo "[+] Creating /etc/kratos/repos.d/00-official.conf..."
# NOTA: "url" deve puntare ESATTAMENTE alla cartella che contiene
# index.json (non alla root del repo), perché kratos-repo.c costruisce
# l'URL di download come "<url>/<pkg.url>" e nell'index.json generato
# da scripts/generate-index.sh il campo "url" è relativo a questa
# cartella (es. "packages/hello-2.12-1-x86_64.kpkg").
cat > "$ETC/kratos/repos.d/00-official.conf" <<'EOF'
[kratos-official]
url=https://raw.githubusercontent.com/PIXELQUADRO07/KratosOS-Packages/main/repository/x86_64/stable
enabled=yes
priority=100
EOF

echo "[+] Creating /etc/rc.sysinit..."
cat > "$ETC/rc.sysinit" <<'EOF'
#!/bin/bash
# /etc/rc.sysinit — KratosOS Early System Initialization

export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export TERM=linux

# Reduce kernel message verbosity to avoid interleaving with login/shell
dmesg -n 1

echo "
  Initializing KratosOS $(cat /etc/os-release | grep VERSION_ID | cut -d'=' -f2 | tr -d '\"')
"

# 1. Mount virtual filesystems (init handles core, but we double-check)
[ -d /proc/self ] || mount -t proc proc /proc
[ -d /sys/kernel ] || mount -t sysfs sysfs /sys
[ -d /dev/pts ] || mkdir -p /dev/pts && mount -t devpts devpts /dev/pts -o gid=5,mode=620

# 2. Setup /etc/mtab
ln -sf /proc/self/mounts /etc/mtab 2>/dev/null || true

# 3. Generate D-Bus machine-id if missing
if [ ! -f /etc/machine-id ]; then
    echo "[rc.sysinit] Generating /etc/machine-id..."
    # Use kernel UUID as a source for machine-id
    cat /proc/sys/kernel/random/uuid | tr -d '-' > /etc/machine-id
fi

# 4. Update shared library cache
if [ -x /sbin/ldconfig ]; then
    echo "[rc.sysinit] Updating shared library cache..."
    ldconfig
fi

# 5. Clean up temporary files from previous boot
echo "[rc.sysinit] Cleaning /tmp and /run..."
rm -rf /run/* /tmp/*
mkdir -p /run/lock /run/user /run/shm
chmod 1777 /tmp /run/shm

# 4. Initialize random seed (if possible)
[ -f /var/lib/urandom/seed ] && cat /var/lib/urandom/seed > /dev/urandom

# 5. Populate /etc/issue dynamically if needed
# (currently static in skeleton)

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

echo "[+] Pre-registering base system packages in KPM database..."
# This ensures that packages depending on glibc or kpm find them as "installed"
mkdir -p "$SYSROOT/var/lib/kratos/db/packages"
cat > "$SYSROOT/var/lib/kratos/db/packages/glibc" <<EOF
name=glibc
version=2.42
release=1
arch=x86_64
description=GNU C Library (Base System)
EOF

cat > "$SYSROOT/var/lib/kratos/db/packages/kpm" <<EOF
name=kpm
version=0.7.8
release=1
arch=x86_64
description=Kratos Package Manager (Base System)
EOF

cat > "$SYSROOT/var/lib/kratos/db/packages/ncurses" <<EOF
name=ncurses
version=6.5
release=1
arch=x86_64
description=Ncurses Libraries (Base System)
EOF

cat > "$SYSROOT/var/lib/kratos/db/packages/readline" <<EOF
name=readline
version=8.2
release=1
arch=x86_64
description=GNU Readline Library (Base System)
EOF

cat > "$SYSROOT/var/lib/kratos/db/packages/bash" <<EOF
name=bash
version=5.3
release=1
arch=x86_64
description=GNU Bourne-Again SHell (Base System)
EOF

cat > "$SYSROOT/var/lib/kratos/db/packages/coreutils" <<EOF
name=coreutils
version=9.7
release=1
arch=x86_64
description=GNU Core Utilities (Base System)
EOF

cat > "$SYSROOT/var/lib/kratos/db/packages/grep" <<EOF
name=grep
version=3.11
release=1
arch=x86_64
description=GNU Grep (Base System)
EOF

cat > "$SYSROOT/var/lib/kratos/db/packages/sed" <<EOF
name=sed
version=4.9
release=1
arch=x86_64
description=GNU Stream Editor (Base System)
EOF

cat > "$SYSROOT/var/lib/kratos/db/packages/findutils" <<EOF
name=findutils
version=4.10.0
release=1
arch=x86_64
description=GNU Find Utilities (Base System)
EOF

cat > "$SYSROOT/var/lib/kratos/db/packages/tar" <<EOF
name=tar
version=1.35
release=1
arch=x86_64
description=GNU Tape Archiver (Base System)
EOF

cat > "$SYSROOT/var/lib/kratos/db/packages/gzip" <<EOF
name=gzip
version=1.14
release=1
arch=x86_64
description=GNU Data Compression (Base System)
EOF

cat > "$SYSROOT/var/lib/kratos/db/packages/mbedtls" <<EOF
name=mbedtls
version=3.6.3
release=1
arch=x86_64
description=Mbed TLS Library (Base System)
EOF

cat > "$SYSROOT/var/lib/kratos/db/packages/zlib" <<EOF
name=zlib
version=1.3.1
release=1
arch=x86_64
description=Zlib Compression Library (Base System)
EOF

cat > "$SYSROOT/var/lib/kratos/db/packages/zstd" <<EOF
name=zstd
version=1.5.6
release=1
arch=x86_64
description=Zstandard Compression (Base System)
EOF

echo
echo "[+] /etc skeleton created successfully."
echo
echo "Files:"
find "$ETC" -maxdepth 2 -printf "  %P\n" | sort
