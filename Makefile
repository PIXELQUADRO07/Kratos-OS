# KratosOS — Root Makefile
#
# Convenience wrapper around the build scripts.
# Run `make help` to see all available targets.
#
# Usage:
#   make phase1          # full Phase 1 toolchain bootstrap
#   make phase2          # full Phase 2 userspace base
#   make toolchain       # alias for phase1
#   make verify          # run Phase 1 toolchain verification
#   make verify-phase2   # run Phase 2 userspace verification
#   make download        # download all sources
#   make clean           # remove work/ tools/ sysroot/ (keep downloads/)
#   make distclean       # remove everything including downloads/

SHELL         := /bin/bash
SCRIPTS       := build/scripts
MAKEFLAGS     += --no-print-directory

# Export KRATOS_JOBS so sub-scripts pick it up (default: nproc)
export KRATOS_JOBS ?= $(shell nproc)

.PHONY: help phase1 phase2 toolchain verify verify-phase2 download clean distclean \
        gcc-pass1 gcc-pass2 binutils linux-headers glibc libgcc \
        ncurses readline bash coreutils grep sed gawk findutils \
        diffutils tar gzip xz bzip2 file-cmd

# ─────────────────────────────────────────────
# Default: show help
# ─────────────────────────────────────────────
help:
	@echo ""
	@echo "  KratosOS Build System"
	@echo "  ─────────────────────────────────────────"
	@echo "  make phase1        Full Phase 1 toolchain bootstrap"
	@echo "  make phase2        Full Phase 2 userspace base"
	@echo "  make toolchain     Alias for phase1"
	@echo "  make verify        Run Phase 1 verification"
	@echo "  make verify-phase2 Run Phase 2 verification"
	@echo "  make download      Download all source tarballs"
	@echo ""
	@echo "  Phase 1 individual stages:"
	@echo "  make linux-headers"
	@echo "  make binutils"
	@echo "  make gcc-pass1"
	@echo "  make glibc-bootstrap"
	@echo "  make libgcc"
	@echo "  make glibc"
	@echo "  make gcc-pass2"
	@echo ""
	@echo "  Phase 2 individual packages:"
	@echo "  make ncurses  readline  bash  coreutils"
	@echo "  make grep  sed  gawk  findutils  diffutils"
	@echo "  make tar  gzip  xz  bzip2  file-cmd"
	@echo ""
	@echo "  make clean       Remove build artifacts (keep downloads)"
	@echo "  make distclean   Remove everything including downloads"
	@echo ""
	@echo "  Options:"
	@echo "  KRATOS_JOBS=N    Parallel make jobs (default: nproc=$(shell nproc))"
	@echo ""

# ─────────────────────────────────────────────
# Phase 1 — Full toolchain bootstrap
# ─────────────────────────────────────────────
phase1 toolchain:
	@bash $(SCRIPTS)/build-all-phase1.sh

verify:
	@bash $(SCRIPTS)/verify-toolchain.sh

download:
	@bash $(SCRIPTS)/download.sh

# ─────────────────────────────────────────────
# Individual stages (useful for re-running one step)
# ─────────────────────────────────────────────
linux-headers:
	@bash $(SCRIPTS)/install-linux-headers.sh

binutils:
	@bash $(SCRIPTS)/build-binutils.sh

gcc-pass1:
	@bash $(SCRIPTS)/build-gcc-pass1.sh

glibc-bootstrap:
	@bash $(SCRIPTS)/build-glibc-bootstrap.sh

libgcc:
	@bash $(SCRIPTS)/build-libgcc.sh

glibc:
	@bash $(SCRIPTS)/build-glibc.sh

gcc-pass2:
	@bash $(SCRIPTS)/build-gcc-pass2.sh

# ─────────────────────────────────────────────
# Phase 2 — Userspace base
# ─────────────────────────────────────────────
phase2:
	@bash $(SCRIPTS)/build-all-phase2.sh

verify-phase2:
	@bash $(SCRIPTS)/verify-phase2.sh

ncurses:
	@bash $(SCRIPTS)/build-ncurses.sh

readline:
	@bash $(SCRIPTS)/build-readline.sh

bash:
	@bash $(SCRIPTS)/build-bash.sh

coreutils:
	@bash $(SCRIPTS)/build-coreutils.sh

grep:
	@bash $(SCRIPTS)/build-grep.sh

sed:
	@bash $(SCRIPTS)/build-sed.sh

gawk:
	@bash $(SCRIPTS)/build-gawk.sh

findutils:
	@bash $(SCRIPTS)/build-findutils.sh

diffutils:
	@bash $(SCRIPTS)/build-diffutils.sh

tar:
	@bash $(SCRIPTS)/build-tar.sh

gzip:
	@bash $(SCRIPTS)/build-gzip.sh

xz:
	@bash $(SCRIPTS)/build-xz.sh

bzip2:
	@bash $(SCRIPTS)/build-bzip2.sh

file-cmd:
	@bash $(SCRIPTS)/build-file.sh

# ─────────────────────────────────────────────
# Cleanup
# ─────────────────────────────────────────────
clean:
	@echo "[+] Removing build/work/, build/tools/, build/sysroot/ ..."
	@rm -rf build/work build/tools build/sysroot build/sources
	@echo "[✓] Clean done. Downloads preserved."

distclean: clean
	@echo "[+] Removing build/downloads/ ..."
	@rm -rf build/downloads
	@echo "[✓] Distclean done."
