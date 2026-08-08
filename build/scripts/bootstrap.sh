#!/usr/bin/env bash

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

echo "================================"
echo "       MYDISTRO BOOTSTRAP"
echo "================================"
echo
echo "Project root: $PROJECT_ROOT"
echo

mkdir -p "$PROJECT_ROOT/build/work"
mkdir -p "$PROJECT_ROOT/build/downloads"
mkdir -p "$PROJECT_ROOT/build/sources"
mkdir -p "$PROJECT_ROOT/build/tools"
mkdir -p "$PROJECT_ROOT/build/sysroot"

echo "[+] Build directories created."
