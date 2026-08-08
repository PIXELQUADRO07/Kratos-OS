#!/usr/bin/env bash
# build-all-phase2.sh — Orchestrate the full Phase 2 userspace base build.
#
# Build order (dependency-aware):
#   1.  ncurses      (terminal lib — needed by readline, bash)
#   2.  readline     (line editing — needed by bash)
#   3.  bash         (shell — /bin/bash + /bin/sh symlink)
#   4.  coreutils    (ls, cp, mv, rm, cat, echo, chmod, ...)
#   5.  grep
#   6.  sed
#   7.  gawk
#   8.  findutils    (find, xargs)
#   9.  diffutils    (diff, cmp)
#   10. tar
#   11. gzip
#   12. xz
#   13. bzip2
#   14. file

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../config/build.conf"
source "$SCRIPT_DIR/../config/versions.conf"

TOTAL=14
STEP=0

banner() {
    local title="$1"
    echo
    echo "══════════════════════════════════════════"
    printf "  [Phase 2 — %2d/%d] %s\n" "$STEP" "$TOTAL" "$title"
    echo "──────────────────────────────────────────"
}

run_step() {
    local name="$1"
    local script="$2"
    STEP=$((STEP + 1))
    banner "$name"
    local t0=$SECONDS
    if bash "$SCRIPT_DIR/$script" 2>&1; then
        echo "  ✓ Done in $((SECONDS - t0))s"
    else
        echo
        echo "[✗] Phase 2 FAILED at step $STEP/$TOTAL: $name"
        echo "    Script: $SCRIPT_DIR/$script"
        exit 2
    fi
}

echo
echo "════════════════════════════════════════════"
echo "       KRATOSOS PHASE 2 — USERSPACE BASE"
echo "════════════════════════════════════════════"
echo "  Target:  $TARGET"
echo "  Sysroot: $KRATOS_SYSROOT"
echo "  Jobs:    ${KRATOS_JOBS:-$(nproc)}"
echo

run_step "ncurses (terminal library)"    build-ncurses.sh
run_step "readline (line editing)"       build-readline.sh
run_step "bash (shell)"                  build-bash.sh
run_step "coreutils (ls, cp, mv, ...)"   build-coreutils.sh
run_step "grep"                          build-grep.sh
run_step "sed"                           build-sed.sh
run_step "gawk"                          build-gawk.sh
run_step "findutils (find, xargs)"       build-findutils.sh
run_step "diffutils (diff, cmp)"         build-diffutils.sh
run_step "tar"                           build-tar.sh
run_step "gzip"                          build-gzip.sh
run_step "xz"                            build-xz.sh
run_step "bzip2"                         build-bzip2.sh
run_step "file"                          build-file.sh

echo
echo "════════════════════════════════════════════"
echo "    Phase 2 Complete — Running verification"
echo "════════════════════════════════════════════"
bash "$SCRIPT_DIR/verify-phase2.sh"
