#!/usr/bin/env bash

# build-ca-certificates.sh — Install Mozilla CA certificate bundle into KratosOS sysroot

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"

SYSROOT="$KRATOS_SYSROOT"
CA_DIR="$SYSROOT/etc/ssl/certs"
CA_BUNDLE="$CA_DIR/ca-certificates.crt"

# This bundle is the root of trust for every HTTPS connection KratosOS
# will ever make (including, eventually, package signature bootstrapping),
# so it is pinned to a known-good sha256 rather than trusted blindly.
#
# curl.se serves it over HTTPS so it isn't sent in the clear, but that's
# not the same as an independent integrity check. Update this pin
# deliberately — as a reviewed action, e.g. when rotating CAs every
# 6-12 months — never automatically on every build.
#
# To (re)generate this pin after reviewing a fresh download:
#   sha256sum "$KRATOS_BUILD/downloads/cacert.pem"
CA_BUNDLE_SHA256=""

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

# Verify integrity against the pinned hash before trusting this file
# with anything. This check applies whether the file was just
# downloaded or came from the local cache.
ACTUAL_SHA256=$(sha256sum "$CA_DOWNLOAD" | awk '{print $1}')

if [ -z "$CA_BUNDLE_SHA256" ]; then
    echo "[!] CA_BUNDLE_SHA256 is not set in this script — refusing to install unverified."
    echo "    Review the downloaded bundle yourself, then pin its hash:"
    echo "      $CA_DOWNLOAD"
    echo "      sha256 = $ACTUAL_SHA256"
    echo "    Paste that value into CA_BUNDLE_SHA256 at the top of this script and re-run."
    exit 1
fi

if [ "$ACTUAL_SHA256" != "$CA_BUNDLE_SHA256" ]; then
    echo "[!] CA bundle sha256 mismatch — refusing to install."
    echo "    Expected: $CA_BUNDLE_SHA256"
    echo "    Actual:   $ACTUAL_SHA256"
    echo "    This means either curl.se served a different bundle than the one you"
    echo "    pinned, or the cached copy at $CA_DOWNLOAD is stale/corrupted."
    echo "    Remove the cached file and re-run to fetch fresh, then re-verify by hand"
    echo "    before updating the pin."
    exit 1
fi

echo "[✓] CA bundle sha256 verified."

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
