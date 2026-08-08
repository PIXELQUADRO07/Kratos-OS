#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

mkdir -p "$KRATOS_DOWNLOADS"
mkdir -p "$KRATOS_SOURCES"

echo "================================"
echo "       KRATOSOS SOURCES"
echo "================================"
echo
echo "Linux:    $LINUX_VERSION"
echo "Binutils: $BINUTILS_VERSION"
echo "GCC:      $GCC_VERSION"
echo "Glibc:    $GLIBC_VERSION"
echo
echo "Downloads: $KRATOS_DOWNLOADS"
echo "Sources:   $KRATOS_SOURCES"
