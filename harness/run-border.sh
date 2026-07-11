#!/usr/bin/env bash
#
# run-border.sh — run the harness's left-border check against the lborder effect:
# verify the effect actually opens the left border on a band of scanlines.
# Requires python3 with numpy + Pillow.

set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

exec python3 -u "$REPO/harness/diff_harness.py" \
  --hatari "$REPO/external/hatari/build/src/hatari" \
  --tos "${TALOS_TOS:-$REPO/tos/etos512uk.img}" \
  --effect "$REPO/tests/effects/disk-lborder" \
  --border-check "$@"
