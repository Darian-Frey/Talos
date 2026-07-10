#!/usr/bin/env bash
#
# run.sh — run the per-scanline validation harness against the raster-bar effect
# with the repo's default Hatari / TOS / effect paths. Extra args pass through
# (e.g. --reg <hex>). Requires python3 with numpy + Pillow.

set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

exec python3 -u "$REPO/harness/diff_harness.py" \
  --hatari "$REPO/external/hatari/build/src/hatari" \
  --tos "${TALOS_TOS:-$REPO/tos/etos512uk.img}" \
  --effect "$REPO/tests/effects/disk" "$@"
