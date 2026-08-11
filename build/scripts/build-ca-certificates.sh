#!/usr/bin/env bash

# build-ca-certificates.sh — Install Mozilla CA certificate bundle into KratosOS sysroot

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"

SYSROOT="$KRATOS_SYSROOT"
CA_DIR="$SYSROOT/etc/ssl/certs"
CA_BUNDLE="$CA_DIR/ca-certificates.crt"

echo "========================================"
echo "       KRATOSOS CA CERTIFICATES"
echo "========================================"
echo

mkdir -p "$CA_DIR"

# Download Mozilla CA bundle from curl.se (canonical source, always up-to-date)
CA_DOWNLOAD="$KRATOS_DOWNLOADS/cacert.pem"
if [ ! -f "$CA_DOWNLOAD" ]; then
    echo "[+] Downloading Mozilla CA certificate bundle..."
    mkdir -p "$KRATOS_DOWNLOADS"
    curl -L --progress-bar --retry 3 \
        -o "${CA_DOWNLOAD}.part" \
        "https://curl.se/ca/cacert.pem"
    mv "${CA_DOWNLOAD}.part" "$CA_DOWNLOAD"
    echo "[✓] Downloaded."
else
    echo "[=] CA bundle already downloaded."
fi

echo "[+] Installing CA bundle into sysroot..."
cp "$CA_DOWNLOAD" "$CA_BUNDLE"

# Also create a symlink at the standard alternate location
mkdir -p "$SYSROOT/etc/pki/tls/certs"
ln -sf /etc/ssl/certs/ca-certificates.crt "$SYSROOT/etc/pki/tls/certs/ca-bundle.crt"

CERT_COUNT=$(grep -c '^-----BEGIN CERTIFICATE-----' "$CA_BUNDLE")
echo "[✓] Installed $CERT_COUNT CA certificates."
echo "  Path: /etc/ssl/certs/ca-certificates.crt"
echo
echo "[✓] CA certificates installation complete."
