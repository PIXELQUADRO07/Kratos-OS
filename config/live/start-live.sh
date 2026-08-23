#!/bin/bash
# /etc/live/start-live.sh — Live Environment Initialization and Graphical Boot
#
# Inspired by Parrot OS / Debian Live (live-config) workflows.

echo "[Live] Initializing KratosOS Live Environment..."

LIVE_USER="kratos-live"
LIVE_HOME="/home/kratos-live"
LIVE_UID=1000

# 1. Hardware Wait Loop (Parrot OS style)
# Real hardware can be slower than QEMU at initializing DRM drivers.
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
    echo "[Live] Warning: no graphics device detected after 15s. X might fail."
fi

# 2. Ensure /run/dbus directory and system dbus are available
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

# 3. Setup session home environment
echo "[Live] Preparing $SESSION_USER desktop..."
if [ -f /etc/live/kratosos-live.desktop ]; then
    cp /etc/live/kratosos-live.desktop "$SESSION_HOME/Desktop/"
    chmod +x "$SESSION_HOME/Desktop/kratosos-live.desktop"
fi
cp /etc/live/xinitrc "$SESSION_HOME/.xinitrc" 2>/dev/null || true
chmod +x "$SESSION_HOME/.xinitrc" 2>/dev/null || true
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

    # Foreground startx: 99-live already launched us in the background.
    if [ "$SESSION_USER" = "root" ]; then
        eval "$STARTX_CMD" >>/var/log/Xorg.start.log 2>&1
        STARTX_RC=$?
    else
        su - "$SESSION_USER" -c "$STARTX_CMD" >>/var/log/Xorg.start.log 2>&1
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
