# KratosOS — Root Makefile
#
# Convenience wrapper around the build scripts.
# Run `make help` to see all available targets.
#
# Usage:
#   make phase1          # full Phase 1 toolchain bootstrap
#   make toolchain       # alias for phase1
#   make verify          # run toolchain verification only
#   make download        # download all sources
#   make clean           # remove work/ tools/ sysroot/ (keep downloads/)
#   make distclean       # remove everything including downloads/

SHELL         := /bin/bash
SCRIPTS       := build/scripts
MAKEFLAGS     += --no-print-directory

# Export KRATOS_JOBS so sub-scripts pick it up (default: nproc)
export KRATOS_JOBS ?= $(shell nproc)

.PHONY: help phase1 toolchain verify download clean distclean \
        gcc-pass1 gcc-pass2 binutils linux-headers glibc libgcc

# ─────────────────────────────────────────────
# Default: show help
# ─────────────────────────────────────────────
help:
	@echo ""
	@echo "  KratosOS Build System"
	@echo "  ─────────────────────────────────────────"
	@echo "  make phase1      Full Phase 1 toolchain bootstrap"
	@echo "  make toolchain   Alias for phase1"
	@echo "  make verify      Run verify-toolchain.sh only"
	@echo "  make download    Download all source tarballs"
	@echo ""
	@echo "  Individual stages:"
	@echo "  make linux-headers"
	@echo "  make binutils"
	@echo "  make gcc-pass1"
	@echo "  make glibc-bootstrap"
	@echo "  make libgcc"
	@echo "  make glibc"
	@echo "  make gcc-pass2"
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
