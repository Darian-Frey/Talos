#!/usr/bin/env bash
#
# bootstrap-vasm.sh — build the vasm 68k assembler locally (no sudo).
#
# vasm (Volker Barthelmann / Frank Wille) is the standard ST-scene assembler and
# can emit a ready-to-run TOS executable directly (-Ftos). Talos uses it to build
# the test effects under tests/effects/ (and, later, Phase 4 asm-stub export).
# It lands in external/vasm-src/ (gitignored). No dependencies beyond a C compiler.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${REPO_ROOT}/external/vasm-src"
URL="${TALOS_VASM_URL:-http://sun.hasenbraten.de/vasm/release/vasm.tar.gz}"
BIN="${DEST}/vasm/vasmm68k_mot"

mkdir -p "${DEST}"
if [ ! -f "${DEST}/vasm/Makefile" ]; then
  echo ">> downloading vasm from ${URL}"
  curl -sL --max-time 120 -o "${DEST}/vasm.tar.gz" "${URL}"
  tar xzf "${DEST}/vasm.tar.gz" -C "${DEST}"
fi

echo ">> building vasm (m68k, Motorola syntax)"
make -C "${DEST}/vasm" CPU=m68k SYNTAX=mot

echo ">> done: ${BIN}"
"${BIN}" 2>&1 | head -1 || true
