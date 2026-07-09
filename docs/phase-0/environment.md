# Phase 0 — Development environment

> **Status:** Active
> **Provenance:** Probed on the development ThinkPad P15 by Claude (implementer) during Phase 0 bootstrap.
> **Last reviewed:** 2026-07-09
> **Why this status:** Records the verified toolchain state at the start of implementation. Update when the machine or pinned dependencies change.

---

## Target machine

- ThinkPad P15 Gen 2i, Ubuntu 24.04 (kernel 6.17), x86_64. Linux-first per README.

## Toolchain present (verified 2026-07-09)

| Component | Version | Notes |
|---|---|---|
| git | 2.43.0 | |
| CMake | 3.28.3 | ≥ 3.21 required by top-level build |
| g++ | 13.3.0 | C++17 |
| GNU Make | 4.3 | build generator |
| Qt6 | 6.4.2 | Core, Gui, Widgets, Network, OpenGLWidgets all present |
| zlib / libpng / readline | 1.3 / 1.6.43 / 8.2 | Hatari optional deps, present |

## Missing / to install

- **SDL2** (`libsdl2-dev`) — **required** to build the Hatari fork. Not yet installed.
- **ninja** — optional; Make is sufficient.

Install line (Ubuntu):

```bash
sudo apt install libsdl2-dev
# (full set, if starting clean:)
# sudo apt install build-essential cmake git qt6-base-dev libsdl2-dev \
#      zlib1g-dev libpng-dev libreadline-dev
```

Run `scripts/check-deps.sh` to re-verify at any time.

## Hatari fork (D-003)

- Repo: `https://github.com/tattlemuss/hatari.git`
- Branch: `debugger-extensions`
- Pinned commit at bootstrap: `9832e006bf9d6e8c6fe2edc56156860d2e8145e0` (verified reachable 2026-07-09)
- Cloned/built by `scripts/bootstrap-hatari.sh` into `external/hatari/` (gitignored).

## Fork build + launch facts (verified 2026-07-09)

- Fork builds clean: **Hatari v2.5.0**, binary at `external/hatari/build/src/hatari`.
  Configure found SDL2 2.30.0, readline, zlib, png, capstone (disassembly).
- **Remote-debug socket: TCP port 56001** (`RDB_PORT`), protocol id `0x1007`.
  `RemoteDebug_Init()` (main.c:799) opens the listener **unconditionally** — no
  launch flag needed to enable it.
- **But** the listener only comes up on a *successful boot*: a headless run with no
  valid TOS errors out before binding 56001. So a TOS image is on the M0 critical
  path (C-009), not optional for proving the pipe.
- Headless launch recipe (for CI / socket tests):
  `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy hatari --sound off --tos <rom>`
- **Pipe proven end to end (2026-07-09):** with EmuTOS booting, a raw TCP client
  connected to `127.0.0.1:56001`, received the `!connected|1007` handshake, and
  read live beam counters via `regs` (HBL/LineCycles/FrameCycles/VBL). See
  `protocol/b1-protocol.md`. This is the M0 pipe (minus the Qt client + framebuffer).

## TOS decision (C-009)

- **EmuTOS** = development/CI default (free, GPL-compatible, legal to redistribute).
- Real ROMs for fidelity/validation, user-supplied, dropped in `tos/` (gitignored):
  TOS 1.04 (520/1040 ST, Mega ST), TOS 1.62 (STE), TOS 2.06 (Mega STE).
- The per-scanline diff harness (D-009) only needs *the same* ROM on both sides,
  so EmuTOS suffices for validation; real ROMs are about real-world behaviour.
