#!/bin/bash
# /etc/live/start-live.sh — Live Environment Initialization and Graphical Boot

echo "[Live] Initializing KratosOS Live Environment..."

# 1. Ensure /run/dbus directory and system dbus are available
mkdir -p /run/dbus /run/user/1000
chown 18:18 /run/dbus 2>/dev/null || true
chown kratos-live:kratos-live /run/user/1000 2>/dev/null || true

if command -v dbus-daemon >/dev/null 2>&1 && [ ! -e /run/dbus/system_bus_socket ]; then
    echo "[Live] Starting system D-Bus daemon..."
    dbus-daemon --system --fork 2>/dev/null || true
fi

# 2. Setup user home environment
if [ -d /home/kratos-live ]; then
    mkdir -p /home/kratos-live/Desktop
    if [ -f /etc/live/kratosos-live.desktop ]; then
        cp /etc/live/kratosos-live.desktop /home/kratos-live/Desktop/
        chmod +x /home/kratos-live/Desktop/kratosos-live.desktop
    fi
    cp /etc/live/xinitrc /home/kratos-live/.xinitrc 2>/dev/null || true
    chmod +x /home/kratos-live/.xinitrc
    chown -R kratos-live:kratos-live /home/kratos-live 2>/dev/null || true
fi

# 3. Launch X11 GUI as kratos-live user if startx is present
if command -v startx >/dev/null 2>&1; then
    echo "[Live] Starting graphical XFCE session..."
    su - kratos-live -c "startx /etc/live/xinitrc -- vt7" &
fi
