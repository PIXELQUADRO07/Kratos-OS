#!/bin/bash
# /etc/live/start-live.sh — Live Environment Initialization and Graphical Boot
#
# Inspired by Parrot OS / Debian Live (live-config) workflows.

echo "[Live] Initializing KratosOS Live Environment..."

LIVE_USER="kratos-live"
LIVE_HOME="/home/kratos-live"
LIVE_UID=1000

# 1. Hardware Wait Loop (Parrot OS style)
# Real hardware / VirtualBox can be slower at initializing DRM/KMS drivers.
echo "[Live] Waiting for graphics device..."
READY=0
for i in $(seq 1 15); do
    if [ -e /dev/dri/card0 ] || [ -e /dev/fb0 ]; then
        echo "[Live] Graphics device ready."
        READY=1
        break
    fi
    sleep 1
done

if [ "$READY" -eq 0 ]; then
    echo "[Live] Warning: no graphics device detected after 15s. X might fall back to VESA/FBDEV."
fi

# 2. Configure system permissions and directories for X11 & D-Bus
chmod 1777 /tmp 2>/dev/null || true
mkdir -p /var/log /var/lib/xkb /etc/X11
chmod 777 /var/log /var/lib/xkb 2>/dev/null || true
chmod 4755 /usr/bin/Xorg 2>/dev/null || true

cat > /etc/X11/Xwrapper.config << 'EOF'
allowed_users = anybody
needs_root_rights = yes
EOF

mkdir -p /run/dbus /run/user/0 "/run/user/$LIVE_UID"
chown 18:18 /run/dbus 2>/dev/null || true

if id "$LIVE_USER" >/dev/null 2>&1; then
    SESSION_USER="$LIVE_USER"
    SESSION_HOME="$LIVE_HOME"
    SESSION_RUNTIME="/run/user/$LIVE_UID"
    mkdir -p "$SESSION_HOME" "$SESSION_HOME/Desktop" "$SESSION_RUNTIME"
    chown -R "$LIVE_USER:$LIVE_USER" "$SESSION_HOME" "$SESSION_RUNTIME" 2>/dev/null || true
    chmod 700 "$SESSION_RUNTIME"
else
    echo "[Live] User $LIVE_USER not found, falling back to root."
    SESSION_USER="root"
    SESSION_HOME="/root"
    SESSION_RUNTIME="/run/user/0"
    mkdir -p "$SESSION_HOME" "$SESSION_HOME/Desktop" "$SESSION_RUNTIME"
    chmod 700 "$SESSION_RUNTIME"
fi

export XDG_RUNTIME_DIR="$SESSION_RUNTIME"

if command -v dbus-daemon >/dev/null 2>&1 && [ ! -e /run/dbus/system_bus_socket ]; then
    echo "[Live] Starting system D-Bus daemon..."
    dbus-daemon --system --fork 2>/dev/null || true
    sleep 1
fi

# 3. Setup session home environment and synchronize xinitrc across all profiles
echo "[Live] Preparing $SESSION_USER desktop..."
for dest in /etc/X11/xinit/xinitrc /root/.xinitrc "$SESSION_HOME/.xinitrc" /etc/skel/.xinitrc; do
    mkdir -p "$(dirname "$dest")"
    cp -f /etc/live/xinitrc "$dest" 2>/dev/null || true
    chmod +x "$dest" 2>/dev/null || true
done

if [ -f /etc/live/kratosos-live.desktop ]; then
    cp -f /etc/live/kratosos-live.desktop "$SESSION_HOME/Desktop/" 2>/dev/null || true
    chmod +x "$SESSION_HOME/Desktop/kratosos-live.desktop" 2>/dev/null || true
    cp -f /etc/live/kratosos-live.desktop "/root/Desktop/" 2>/dev/null || true
    chmod +x "/root/Desktop/kratosos-live.desktop" 2>/dev/null || true
fi

chown -R "$SESSION_USER:$SESSION_USER" "$SESSION_HOME" 2>/dev/null || true

# 4. Launch X11 GUI on VT7 (this script is already backgrounded by rc.d)
if command -v startx >/dev/null 2>&1; then
    HAVE_VTSWITCH=0
    if command -v kratos-vtswitch >/dev/null 2>&1; then
        HAVE_VTSWITCH=1
        echo "[Live] Switching to VT7 before starting X..."
        kratos-vtswitch 7 || echo "[Live] Warning: could not switch to VT7."
    fi

    echo "[Live] Starting graphical XFCE session as $SESSION_USER..."
    mkdir -p /var/log
    echo "[Live] Invoking startx..." >> /var/log/Xorg.start.log

    STARTX_CMD="export HOME=$SESSION_HOME USER=$SESSION_USER LOGNAME=$SESSION_USER XDG_RUNTIME_DIR=$SESSION_RUNTIME XDG_SESSION_TYPE=x11; exec startx /etc/live/xinitrc -- vt7 -logverbose 6"

    STARTX_RC=1
    if [ "$SESSION_USER" != "root" ]; then
        su - "$SESSION_USER" -c "$STARTX_CMD" >>/var/log/Xorg.start.log 2>&1
        STARTX_RC=$?
    fi

    # Fallback to root X session if unprivileged startx failed or if root session was selected
    if [ "$STARTX_RC" -ne 0 ]; then
        echo "[Live] Starting/falling back to root X session..." >> /var/log/Xorg.start.log
        SESSION_USER="root"
        SESSION_HOME="/root"
        SESSION_RUNTIME="/run/user/0"
        mkdir -p "$SESSION_HOME" "$SESSION_HOME/Desktop" "$SESSION_RUNTIME"
        chmod 700 "$SESSION_RUNTIME"
        STARTX_CMD="export HOME=$SESSION_HOME USER=$SESSION_USER LOGNAME=$SESSION_USER XDG_RUNTIME_DIR=$SESSION_RUNTIME XDG_SESSION_TYPE=x11; exec startx /etc/live/xinitrc -- vt7 -logverbose 6"
        eval "$STARTX_CMD" >>/var/log/Xorg.start.log 2>&1
        STARTX_RC=$?
    fi

    if [ "$STARTX_RC" -ne 0 ]; then
        echo "[Live] ERROR: startx exited $STARTX_RC. Checking logs..."
        if [ "$HAVE_VTSWITCH" -eq 1 ]; then
            echo "[Live] Switching back to VT1..."
            kratos-vtswitch 1 || true
        fi
        for log in /var/log/Xorg.0.log /var/log/Xorg.start.log; do
            if [ -f "$log" ]; then
                echo "[Live] --- $log ---"
                cat "$log"
            fi
        done
    fi
else
    echo "[Live] ERROR: 'startx' not found. Graphical session cannot start."
    echo "[Live] Please ensure 'xorg-server' and 'xinit' packages are installed in the sysroot."
fi
