#!/usr/bin/env bash
# ==============================================================================
# Script di installazione automatica del tema GRUB KratosOS per Fedora / Linux
# ==============================================================================
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "[!] Questo script deve essere eseguito come root (es. sudo ./install-theme.sh)"
    exit 1
fi

THEME_SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_THEME_DIR="/boot/grub2/themes/kratosos"
GRUB_DEFAULT_FILE="/etc/default/grub"

echo "=== Installazione Tema GRUB KratosOS ==="

# 1. Copia cartella tema in /boot/grub2/themes/kratosos
echo "[1/4] Installazione dei file del tema in $TARGET_THEME_DIR..."
mkdir -p "$TARGET_THEME_DIR"
cp -a "$THEME_SRC_DIR"/* "$TARGET_THEME_DIR/"

# Crea symlink /boot/grub per compatibilità se necessario
if [ ! -d "/boot/grub" ]; then
    ln -sf "/boot/grub2" "/boot/grub" 2>/dev/null || true
fi

# 2. Configurazione /etc/default/grub
echo "[2/4] Aggiornamento configurazione in $GRUB_DEFAULT_FILE..."
if [ -f "$GRUB_DEFAULT_FILE" ]; then
    # Backup
    cp "$GRUB_DEFAULT_FILE" "${GRUB_DEFAULT_FILE}.bak.$(date +%Y%m%d%H%M%S)"

    # Rimuovi o aggiorna vecchie direttive
    sed -i '/^GRUB_THEME=/d' "$GRUB_DEFAULT_FILE"
    sed -i '/^GRUB_GFXMODE=/d' "$GRUB_DEFAULT_FILE"
    sed -i '/^GRUB_GFXPAYLOAD_LINUX=/d' "$GRUB_DEFAULT_FILE"
    sed -i 's/^GRUB_TERMINAL_OUTPUT="console"/GRUB_TERMINAL_OUTPUT="gfxterm"/' "$GRUB_DEFAULT_FILE"

    cat << 'GRUB_CONF' >> "$GRUB_DEFAULT_FILE"
GRUB_THEME="/boot/grub2/themes/kratosos/theme.txt"
GRUB_GFXMODE="1920x1080,auto"
GRUB_GFXPAYLOAD_LINUX="keep"
GRUB_CONF
fi

# 3. Rigenerazione configurazione GRUB
echo "[3/4] Rigenerazione configurazione GRUB (grub2-mkconfig)..."
if command -v grub2-mkconfig &>/dev/null; then
    grub2-mkconfig -o /boot/grub2/grub.cfg
elif command -v update-grub &>/dev/null; then
    update-grub
elif command -v grub-mkconfig &>/dev/null; then
    grub-mkconfig -o /boot/grub/grub.cfg
fi

echo "[4/4] [✓] Tema GRUB KratosOS installato con successo!"
