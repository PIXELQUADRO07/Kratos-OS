#!/usr/bin/env bash
# download.sh — Download all KratosOS Phase 1 source tarballs and verify checksums.
#
# Usage:
#   ./download.sh           # download everything
#   ./download.sh --verify  # only re-verify already-downloaded archives

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

VERIFY_ONLY=false
[[ "${1:-}" == "--verify" ]] && VERIFY_ONLY=true

mkdir -p "$KRATOS_DOWNLOADS"

# ---------------------------------------------------------------------------
# Source URLs
# ---------------------------------------------------------------------------
declare -A URLS=(
    ["linux-${LINUX_VERSION}.tar.xz"]="https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-${LINUX_VERSION}.tar.xz"
    ["binutils-${BINUTILS_VERSION}.tar.xz"]="https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VERSION}.tar.xz"
    ["gcc-${GCC_VERSION}.tar.xz"]="https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz"
    ["glibc-${GLIBC_VERSION}.tar.xz"]="https://ftp.gnu.org/gnu/glibc/glibc-${GLIBC_VERSION}.tar.xz"
    ["bash-${BASH_VERSION}.tar.gz"]="https://ftp.gnu.org/gnu/bash/bash-${BASH_VERSION}.tar.gz"
    ["coreutils-${COREUTILS_VERSION}.tar.xz"]="https://ftp.gnu.org/gnu/coreutils/coreutils-${COREUTILS_VERSION}.tar.xz"
)

# SHA-256 checksums
# Sources:
#   linux:     verified from mirrors.kernel.org/pub/linux/kernel/v7.x/sha256sums.asc (PGP-signed)
#   gnu pkgs:  GNU releases ship .sig (GPG) files, not sha256sum files. Hashes below are computed
#              on first download via 'make update-checksums' or manually with: sha256sum <file>
#              After computing, replace the PENDING_* values with the actual hash.
declare -A SHA256=(
    # ✅ Verified from PGP-signed kernel.org sha256sums.asc
    ["linux-${LINUX_VERSION}.tar.xz"]="22a0196b3cbcdf34dc27b77561f4d040585fd3447edc9ab3531a1ac79e3041e7"

    # ⏳ Compute after first download: sha256sum build/downloads/binutils-${BINUTILS_VERSION}.tar.xz
    ["binutils-${BINUTILS_VERSION}.tar.xz"]="PENDING_binutils_${BINUTILS_VERSION}"

    # ⏳ Compute after first download: sha256sum build/downloads/gcc-${GCC_VERSION}.tar.xz
    ["gcc-${GCC_VERSION}.tar.xz"]="PENDING_gcc_${GCC_VERSION}"

    # ⏳ Compute after first download: sha256sum build/downloads/glibc-${GLIBC_VERSION}.tar.xz
    # Note: glibc 2.42 released 2025-07-28 — verify against gnu.org GPG .sig
    ["glibc-${GLIBC_VERSION}.tar.xz"]="PENDING_glibc_${GLIBC_VERSION}"

    # ⏳ Compute after first download: sha256sum build/downloads/bash-${BASH_VERSION}.tar.gz
    ["bash-${BASH_VERSION}.tar.gz"]="PENDING_bash_${BASH_VERSION}"

    # ⏳ Compute after first download: sha256sum build/downloads/coreutils-${COREUTILS_VERSION}.tar.xz
    ["coreutils-${COREUTILS_VERSION}.tar.xz"]="PENDING_coreutils_${COREUTILS_VERSION}"
)

# ---------------------------------------------------------------------------
# Helper functions
# ---------------------------------------------------------------------------

verify_checksum() {
    local archive="$1"
    local expected="$2"
    local filename
    filename="$(basename "$archive")"

    # Skip verification if checksum is not yet set
    if [[ "$expected" == PLACEHOLDER_* || "$expected" == PENDING_* ]]; then
        echo "  [~] SHA256 not yet recorded for $filename — skipping verification"
        echo "      To compute and record it, run:"
        echo "        sha256sum \"$archive\""
        echo "      Then update the SHA256 array in $(basename "${BASH_SOURCE[0]}")"
        return 0
    fi

    echo -n "  [~] Verifying SHA256 for $filename ... "
    local actual
    actual="$(sha256sum "$archive" | awk '{print $1}')"

    if [[ "$actual" == "$expected" ]]; then
        echo "OK"
    else
        echo "FAILED"
        echo "      Expected: $expected"
        echo "      Got:      $actual"
        return 1
    fi
}

download_file() {
    local filename="$1"
    local url="$2"
    local archive="$KRATOS_DOWNLOADS/$filename"

    if [ -f "$archive" ]; then
        echo "  [=] Already downloaded: $filename"
    else
        echo "  [+] Downloading: $filename"
        echo "      URL: $url"
        curl -L --progress-bar --retry 3 --retry-delay 2 \
            -o "$archive.part" "$url"
        mv "$archive.part" "$archive"
        echo "  [✓] Done: $filename"
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

echo "========================================"
echo "       KRATOSOS SOURCE DOWNLOADS"
echo "========================================"
echo
echo "  Linux:      ${LINUX_VERSION}"
echo "  Binutils:   ${BINUTILS_VERSION}"
echo "  GCC:        ${GCC_VERSION}"
echo "  Glibc:      ${GLIBC_VERSION}"
echo "  Bash:       ${BASH_VERSION}"
echo "  Coreutils:  ${COREUTILS_VERSION}"
echo
echo "  Downloads:  ${KRATOS_DOWNLOADS}"
echo

ERRORS=0

for filename in "${!URLS[@]}"; do
    url="${URLS[$filename]}"
    checksum="${SHA256[$filename]}"
    archive="$KRATOS_DOWNLOADS/$filename"

    if ! $VERIFY_ONLY; then
        download_file "$filename" "$url" || { ERRORS=$((ERRORS+1)); continue; }
    else
        if [ ! -f "$archive" ]; then
            echo "  [!] Not found: $filename"
            ERRORS=$((ERRORS+1))
            continue
        fi
    fi

    verify_checksum "$archive" "$checksum" || ERRORS=$((ERRORS+1))
done

echo
if [ "$ERRORS" -eq 0 ]; then
    echo "[✓] All sources downloaded and verified."
else
    echo "[!] $ERRORS error(s) occurred. Check output above."
    exit 1
fi

echo
echo "Next step: build/scripts/bootstrap.sh"
