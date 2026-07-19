#!/usr/bin/env bash
#
# corpus.sh — run the whole effect-regression corpus against stock Hatari with
# the repo's default Hatari / TOS paths. Extra args pass through to corpus.py
# (e.g. --only border). Requires python3 with numpy + Pillow.

set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

exec python3 -u "$REPO/harness/corpus.py" \
  --hatari "$REPO/external/hatari/build/src/hatari" \
  --tos "${TALOS_TOS:-$REPO/tos/etos512uk.img}" "$@"
