# Phase 0 — Tap points confirmed against fork source (C-003)

> **Status:** Active
> **Provenance:** Claude (implementer), reading the pinned fork source (tattlemuss/hatari @ 9832e006) on 2026-07-09.
> **Last reviewed:** 2026-07-09
> **Why this status:** File-level confirmation done — every ARCHITECTURE §3 candidate exists at the predicted path. Function-seam mapping (exact hook sites) is the next, deeper pass and is NOT yet done; do not trust B2 effort estimates until it is.

---

Discharges constraint **C-003** at the file level: the ARCHITECTURE §3 tap
candidates were indicative until read in the fork. They are now confirmed to exist.

## Confirmed source locations (fork `src/`)

| ARCHITECTURE §3 subsystem | Real file(s) | Status |
|---|---|---|
| GLUE video timing / beam / border | `src/video.c` (+ `includes/video.h`) | ✓ exists |
| Register write path | `src/ioMem.c`, `src/ioMemTabST.c`, `src/ioMemTabSTE.c` | ✓ exists |
| Blitter | `src/blitter.c` (+ `includes/blitter.h`) | ✓ exists |
| DMA sound + LMC1992/Microwire | `src/dmaSnd.c` | ✓ exists |
| Shifter palette | (in `video.c` / `ioMemTab*` handlers — to pin exactly) | ⧗ file confirmed, function TBD |
| MFP 68901 | `src/mfp.c` | ✓ exists |
| WinUAE 68000 core | `src/cpu/` | ✓ exists |

## B1 remote-protocol surface (the hrdb pipe)

- **`src/debug/remotedebug.c` / `remotedebug.h`** — the remote-debug protocol
  implementation Talos speaks to. This is the primary read for documenting the
  B1 message set in `protocol/README.md`.
- Supporting debug tree: `src/debug/` (debugcpu, debugInfo, breakcond, symbols,
  profile, evaluate, console, …).

## Surprise worth following up

`blitter.c`, `dmaSnd.c`, and `video.c` already contain references to the remote-
debug layer. Upstream may already emit some of the state Talos wants — if so,
several B2 taps shrink to protocol additions with hooks partly in place (helps
C-002, minimal divergence). **Verify per-feature before assuming a from-scratch tap.**

## Not yet done (next pass)

- Map the exact **function seams**: where in `video.c` the beam counters / border
  state machine update; the `ioMem` write dispatch site for the register-write tap;
  the Blitter per-op loop; the DMA drain point; the MFP Timer-B event-count tick.
- Read `remotedebug.c` end to end and write the real B1 message set into
  `protocol/README.md`.
- Only then are B2 effort estimates trustworthy (gates Phase 3).
