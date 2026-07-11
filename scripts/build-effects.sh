#!/usr/bin/env bash
#
# build-effects.sh — assemble the test effects under tests/effects/ with vasm.
#
# Each effect .s is built to an 8.3-named .PRG inside a GEMDOS drive directory's
# AUTO folder, so Hatari can auto-run it at boot (see docs/phase-1/register-writes.md).
# Requires vasm (run scripts/bootstrap-vasm.sh first).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VASM="${REPO_ROOT}/external/vasm-src/vasm/vasmm68k_mot"

if [ ! -x "${VASM}" ]; then
  echo "vasm not found at ${VASM}. Run scripts/bootstrap-vasm.sh first." >&2
  exit 1
fi

# rasterbars: writes the background colour register across the frame.
mkdir -p "${REPO_ROOT}/tests/effects/disk/AUTO"
"${VASM}" -Ftos \
  -o "${REPO_ROOT}/tests/effects/disk/AUTO/RBARS.PRG" \
  "${REPO_ROOT}/tests/effects/rasterbars.s"
echo ">> built tests/effects/disk/AUTO/RBARS.PRG"

# lborder: cycle-exact left-border removal on a band of scanlines.
mkdir -p "${REPO_ROOT}/tests/effects/disk-lborder/AUTO"
"${VASM}" -Ftos \
  -o "${REPO_ROOT}/tests/effects/disk-lborder/AUTO/LB.PRG" \
  "${REPO_ROOT}/tests/effects/lborder.s"
echo ">> built tests/effects/disk-lborder/AUTO/LB.PRG"

# blitfill: continuous blitter copies (F-208), for the blitter-traffic view.
# Needs a machine with a blitter (STE / Mega ST / Mega STE).
mkdir -p "${REPO_ROOT}/tests/effects/disk-blit/AUTO"
"${VASM}" -Ftos \
  -o "${REPO_ROOT}/tests/effects/disk-blit/AUTO/BLIT.PRG" \
  "${REPO_ROOT}/tests/effects/blitfill.s"
echo ">> built tests/effects/disk-blit/AUTO/BLIT.PRG"

# dmasound: looping DMA sample + LMC1992 EQ sweep (F-209), for the DMA/EQ view.
# Needs DMA sound + LMC1992 (STE / Mega STE).
mkdir -p "${REPO_ROOT}/tests/effects/disk-dma/AUTO"
"${VASM}" -Ftos \
  -o "${REPO_ROOT}/tests/effects/disk-dma/AUTO/DMA.PRG" \
  "${REPO_ROOT}/tests/effects/dmasound.s"
echo ">> built tests/effects/disk-dma/AUTO/DMA.PRG"
