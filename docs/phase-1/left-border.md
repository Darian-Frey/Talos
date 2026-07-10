# Phase 1 — Cycle-exact left-border removal

> **Status:** Active
> **Provenance:** Claude (implementer), built and verified (trace + visual) 2026-07-10.
> **Last reviewed:** 2026-07-10
> **Why this status:** Effect works: it opens the left border on a band of scanlines, confirmed by Hatari's border trace and by screenshot. Minor band-edge irregularities remain (see limits).

---

The Phase 1 exit-criterion effect: a cycle-exact routine that removes the ST left
border, so the beam/write overlay and the diff harness have the platform-defining
trick to visualise. Source `tests/effects/lborder.s` → `disk-lborder/AUTO/LB.PRG`.

## The mechanism (as Hatari emulates it)

On a plain STF (WS3), the left border is removed by switching the **resolution
register `$ffff8260`** to hi-res (`2`) then back to lo-res (`0`) at the very start
of a scanline. Hatari sets `BORDERMASK_LEFT_OFF` when it sees hi-res at
`LineCycles <= HDE_On_Hi` (= **4**), i.e. cycle 0–4 of the line (video.c ~1995).
The display then starts ~52 pixels earlier, filling the left border with screen
data on that line.

## The timing (the hard part)

To land the write at `LineCycles <= 4` on every target line, the routine:

- **Syncs on VBL** via `stop #$2300` (low, deterministic wake).
- **Masks MFP interrupts** (`IERA`/`IERB` = 0) so EmuTOS's 200 Hz Timer-C can't
  jitter the sync frame to frame — this was the difference between a jittery,
  corrupted result and a stable band (removals jumped from a few thousand to
  ~1.2M once MFP was silenced).
- **Masks CPU interrupts** (`sr = $2700`) during the timed section.
- Runs a per-scanline loop of **exactly 512 cycles** (`CYCLES_PER_LINE_50HZ`) so
  every switch lands at the same cycle — no drift. The pad was tuned empirically:
  a `dbf` taken ≈ 12 cycles and `nop` = 4 here, and the per-line drift was read
  straight off `--trace video_res` (which prints each res write's `line_cyc_w`).

Tune constants: `DELAY_C` (VBL → first target line) and the loop pad. The feedback
loop is `--trace video_res,video_border_h` — `video_res` shows the exact cycle
each write lands at; `video_border_h` prints `detect remove left N<->M` on success.

## Verification

- Trace: `detect remove left 5<->165` fires ~1.2M times over a run (stable).
- Visual: at VBL 12000 the upper band shows green screen content extending to the
  left frame edge (border removed) while lower scanlines keep the red left border.

## Limits (open polish)

- Band-edge scanlines are slightly irregular (first/last few lines of the band),
  and the exact placement is "good enough to open the border" rather than pinned
  to a single cycle across every line. Fine for a test/demo case; tighten if a
  pixel-exact reference is wanted.
- STF WS3 / PAL / low-res only (matches `BeamGeometry`). Other wakeup states and
  STE prefetch shift the window (Phase 2/3).
- It is a candidate harness regression case, but the harness currently syncs on an
  absolute VBL for the raster effect; adding a border-specific per-scanline check
  (does the left border actually open on the expected lines?) is the next step.
