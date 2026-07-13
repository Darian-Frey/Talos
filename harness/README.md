# Talos — Validation / diff harness (F-213)

> **Status:** Active
> **Provenance:** Claude (implementer), built and passing 2026-07-10.
> **Last reviewed:** 2026-07-12
> **Why this status:** First form of the harness is implemented and green (determinism + non-perturbation). Broadens over the phases (D-009): border-removal case, per machine/region, B2 taps. Phase 4 adds `raster_roundtrip.py` — the prototype→export→verify round-trip.

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

Plus an effect-specific check for the left-border-removal case:

- **Border open** — the lborder effect actually opens the left border on a band of
  scanlines (the left-border column shows screen content, not the border colour).

## Running

```bash
harness/run.sh                 # raster effect: determinism + non-perturbation
harness/run-border.sh          # lborder effect: left border opens on a band
# or directly:
python3 harness/diff_harness.py --hatari <bin> --tos <rom> --effect <gemdos-dir>
python3 harness/diff_harness.py ... --effect <lborder-dir> --border-check
```

Requires `python3` with `numpy` + `Pillow`. Exit code 0 = all checks passed,
1 = per-scanline divergence / no border-open band, 2 = harness error.

## Raster round-trip (F-211/F-212, Phase 4)

`raster_roundtrip.py` proves the prototype→export→verify loop: it codegens a
cycle-exact raster-bar 68k stub from a bar list, assembles it with vasm, runs it
in Hatari, and verifies the authored bar colours reproduce as horizontal bands in
order. The client's Raster workspace drives the same codegen (C++) for preview and
shells this tool for "Verify on Hatari".

```bash
python3 harness/raster_roundtrip.py --hatari <bin> --tos <rom> \
    --bar 20:700 --bar 90:070 --bar 160:007   # (LINE:RGB; default = 7-bar rainbow)
```

## Intra-line split (Phase 4, the Spectrum-512 direction)

`intraline_split.py` proves cycle-*within-line* placement — harder than per-line
bars. It codegens an **HBL-synced** vertical split (colour A, tuned delay, colour
B, re-synced each scanline by the HBL interrupt so it doesn't drift), runs it, and
checks the split is vertical (low column variance) and lands where asked. `--sweep`
maps the HBL delay to the split column — the bench-validated calibration (C-007)
the effect codegen uses.

```bash
python3 harness/intraline_split.py --hatari <bin> --tos <rom> --col 416   # target a column
python3 harness/intraline_split.py --hatari <bin> --tos <rom> --sweep     # recalibrate
python3 harness/intraline_split.py --hatari <bin> --tos <rom> --multi     # Spectrum-512-lite
```

`--multi` packs N colour writes into the HBL handler (no delay) -> N stable
vertical bands per line, the way Spectrum 512 packs 44+ writes/line. Verified at
20 bands (vstd ~6 px), bounded by how many writes fit the ~416-cycle visible line.

`--cols 300,450,600` places boundaries at *arbitrary* target columns via a
per-gap calibrated delay (`col ~= 78 + 76 + 24*L`), and checks each lands within
tolerance. This is the calibration the client's Bands mode uses for click-to-place.

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
