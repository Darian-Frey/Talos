# Talos — Validation / diff harness (F-213)

> **Status:** Active
> **Provenance:** Claude (implementer), built and passing 2026-07-10.
> **Last reviewed:** 2026-07-10
> **Why this status:** First form of the harness is implemented and green (determinism + non-perturbation). Broadens over the phases (D-009): border-removal case, per machine/region, B2 taps.

---

Per-scanline validation (D-009): run an effect through Hatari to a fixed,
deterministic sync point and diff the taken framebuffer **per scanline**.

Because Talos adds no emulation-affecting fork patches at the B1 stage, the
harness checks the two properties that keep it honest:

- **Determinism** — two identical driven runs to VBL `T` produce a pixel-identical
  frame. (Confirms the config/boot is reproducible.)
- **Non-perturbation** — a run that performs a register-write **capture** before
  VBL `T` produces the *same* frame as one that does not. (Confirms Talos's
  break/step/capture driving does not change the emulation.)

Any divergence therefore implicates Talos's driving/config, not the emulation —
the small search space D-009 is built around.

## Running

```bash
harness/run.sh                 # defaults: raster-bar effect, EmuTOS, the fork
harness/run.sh --reg ffff8260  # watch a different register
# or directly:
python3 harness/diff_harness.py --hatari <bin> --tos <rom> --effect <gemdos-dir>
```

Requires `python3` with `numpy` + `Pillow`. Exit code 0 = all checks passed,
1 = per-scanline divergence (rows listed), 2 = harness error.

## How it works

For each run it launches Hatari (fast-forward, headless, clean config) with the
effect from a GEMDOS `AUTO` folder, drives to an absolute VBL sync point (a
deterministic emulation state, chosen above the connect-time VBL so it is
reachable and identical across launches), optionally captures, then screenshots.
`numpy` compares the frames row by row.

## Scope / next

- Uses the raster-bar effect (`tests/effects/`). The Phase 1 exit criterion also
  wants a **border-removal** effect — a harder, cycle-exact case — as a regression
  entry once one exists.
- Grows per D-009: one production per machine/region (Phase 2), Blitter/DMA/dual-
  speed (Phase 3), every exported effect round-trips (Phase 4). When B2 taps land,
  the harness gains its most important job: proving a tap does not alter emulation.
