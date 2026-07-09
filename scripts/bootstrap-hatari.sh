#!/usr/bin/env bash
#
# bootstrap-hatari.sh — clone and build the Hatari fork Talos instruments.
#
# The fork is tattlemuss/hatari, branch `debugger-extensions` — the hrdb-lineage
# remote-debug protocol build (D-003). It is NEVER vendored into this repo: it is
# GPLv2 upstream with its own CMake build, run as a separate process (D-004). We
# track only our own B2 patch series under patches/ (D-005, C-002).
#
# It lands in external/hatari/ (gitignored). A pinned commit keeps the build
# reproducible; bump TALOS_HATARI_REF deliberately, never drift with the branch.

set -euo pipefail

TALOS_HATARI_URL="${TALOS_HATARI_URL:-https://github.com/tattlemuss/hatari.git}"
TALOS_HATARI_BRANCH="${TALOS_HATARI_BRANCH:-debugger-extensions}"
# Pinned 2026-07-09, tip of debugger-extensions at bootstrap. Bump deliberately.
TALOS_HATARI_REF="${TALOS_HATARI_REF:-9832e006bf9d6e8c6fe2edc56156860d2e8145e0}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${REPO_ROOT}/external/hatari"
JOBS="$(nproc 2>/dev/null || echo 4)"

echo ">> Hatari fork : ${TALOS_HATARI_URL}"
echo ">> branch/ref  : ${TALOS_HATARI_BRANCH} @ ${TALOS_HATARI_REF}"
echo ">> destination : ${DEST}"

if [ ! -d "${DEST}/.git" ]; then
  echo ">> cloning..."
  git clone "${TALOS_HATARI_URL}" "${DEST}"
fi

git -C "${DEST}" fetch origin "${TALOS_HATARI_BRANCH}"
git -C "${DEST}" checkout "${TALOS_HATARI_REF}"

echo ">> configuring (CMake)..."
cmake -S "${DEST}" -B "${DEST}/build" -DCMAKE_BUILD_TYPE=Release

echo ">> building (-j${JOBS})..."
cmake --build "${DEST}/build" -j"${JOBS}"

echo
echo ">> done. Hatari binary should be under ${DEST}/build/ (see src/hatari)."
echo ">> A TOS or EmuTOS image is required to run it (C-009); place ROMs in tos/."
