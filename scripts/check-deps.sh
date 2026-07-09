#!/usr/bin/env bash
#
# check-deps.sh — verify the toolchain for building the Talos client and the
# Hatari fork. Read-only; prints a report and exits non-zero if anything the
# build hard-requires is missing.

set -uo pipefail

missing=0

check_cmd() {  # name  command  [--optional]
  local name="$1" cmd="$2" opt="${3:-}"
  if command -v "$cmd" >/dev/null 2>&1; then
    printf '  [ok]   %-14s %s\n' "$name" "$(command -v "$cmd")"
  elif [ "$opt" = "--optional" ]; then
    printf '  [warn] %-14s missing (optional)\n' "$name"
  else
    printf '  [MISS] %-14s missing (required)\n' "$name"; missing=1
  fi
}

check_pkg() {  # name  pkg-config-module  [--optional]
  local name="$1" mod="$2" opt="${3:-}"
  if pkg-config --exists "$mod" 2>/dev/null; then
    printf '  [ok]   %-14s %s\n' "$name" "$(pkg-config --modversion "$mod")"
  elif [ "$opt" = "--optional" ]; then
    printf '  [warn] %-14s missing (optional)\n' "$name"
  else
    printf '  [MISS] %-14s missing (required)\n' "$name"; missing=1
  fi
}

echo "== Build tools =="
check_cmd  git    git
check_cmd  cmake  cmake
check_cmd  "C++"  g++
check_cmd  make   make
check_cmd  ninja  ninja  --optional

echo "== Talos client (Qt6) =="
check_pkg  Qt6Core    Qt6Core
check_pkg  Qt6Widgets Qt6Widgets
check_pkg  Qt6Network Qt6Network

echo "== Hatari fork =="
check_pkg  SDL2     sdl2
check_pkg  zlib     zlib     --optional
check_pkg  libpng   libpng   --optional
check_pkg  readline readline --optional

echo
if [ "$missing" -ne 0 ]; then
  echo "Missing required dependencies. On Ubuntu/Debian, likely:"
  echo "  sudo apt install build-essential cmake git \\"
  echo "       qt6-base-dev libsdl2-dev zlib1g-dev libpng-dev libreadline-dev"
  exit 1
fi
echo "All required dependencies present."
