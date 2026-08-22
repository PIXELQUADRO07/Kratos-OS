#!/bin/bash
# /etc/live/start-live.sh — Live Environment Initialization and Graphical Boot
#
# Inspired by Parrot OS / Debian Live (live-config) workflows.

echo "[Live] Initializing KratosOS Live Environment..."

# 1. Hardware Wait Loop (Parrot OS style)
# Real hardware can be slower than QEMU at initializing DRM drivers.
# We wait for the graphics node to appear before attempting to start X.
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
# D-Bus is mandatory for XFCE session stability.
mkdir -p /run/dbus /run/user/0
chown 18:18 /run/dbus 2>/dev/null || true

# Setup XDG_RUNTIME_DIR for root (Parrot OS style)
export XDG_RUNTIME_DIR=/run/user/0
if [ ! -d "$XDG_RUNTIME_DIR" ]; then
    mkdir -p "$XDG_RUNTIME_DIR"
    chmod 700 "$XDG_RUNTIME_DIR"
fi

if command -v dbus-daemon >/dev/null 2>&1 && [ ! -e /run/dbus/system_bus_socket ]; then
    echo "[Live] Starting system D-Bus daemon..."
    dbus-daemon --system --fork 2>/dev/null || true
    # Give D-Bus a moment to initialize its socket
    sleep 1
fi

# 3. Setup root home environment
echo "[Live] Preparing root desktop..."
mkdir -p /root/Desktop
if [ -f /etc/live/kratosos-live.desktop ]; then
    cp /etc/live/kratosos-live.desktop /root/Desktop/
    chmod +x /root/Desktop/kratosos-live.desktop
fi
cp /etc/live/xinitrc /root/.xinitrc 2>/dev/null || true
chmod +x /root/.xinitrc

# 4. Launch X11 GUI as root on VT7
if command -v startx >/dev/null 2>&1; then
    HAVE_VTSWITCH=0
    if command -v kratos-vtswitch >/dev/null 2>&1; then
        HAVE_VTSWITCH=1
        echo "[Live] Switching to VT7 before starting X..."
        kratos-vtswitch 7 || echo "[Live] Warning: could not switch to VT7."
    fi

    echo "[Live] Starting graphical XFCE session..."
    # We export HOME to ensure Xorg and XFCE find the correct configs.
    export HOME=/root
    startx /etc/live/xinitrc -- vt7 >/var/log/Xorg.start.log 2>&1 &

    # Verify startup
    X_READY=0
    for i in $(seq 1 30); do
        if [ -e /tmp/.X11-unix/X0 ]; then
            X_READY=1
            break
        fi
        sleep 1
    done

    if [ "$X_READY" -eq 1 ]; then
        echo "[Live] X server is up."
    else
        echo "[Live] X server did NOT come up. Checking logs..."
        if [ "$HAVE_VTSWITCH" -eq 1 ]; then
            kratos-vtswitch 1 || true
        fi
        for log in /var/log/Xorg.0.log /var/log/Xorg.start.log; do
            if [ -f "$log" ]; then
                echo "[Live] --- $log ---"
                tail -n 30 "$log"
            fi
        done
    fi
fi
