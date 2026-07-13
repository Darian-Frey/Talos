# Test effects

> **Status:** Active
> **Provenance:** Claude (implementer), Phase 1.
> **Last reviewed:** 2026-07-11
> **Why this status:** First effect (rasterbars) built and verified driving the B1 register-write capture. More (border removal, etc.) come as harder harness cases.

---

Small ST programs that write hardware registers per frame, to drive Talos's
register-write-to-cycle capture (and, later, the validation harness). Assembled
with vasm (`scripts/bootstrap-vasm.sh`, then `scripts/build-effects.sh`).

- `rasterbars.s` — writes the background-colour register `$ffff8240` across the
  frame (rolling colour bands + writes spread over the beam). Built to
  `disk/AUTO/RBARS.PRG`.
- `split.s` — **HBL-synced intra-line raster split** (Phase 4, intra-line pilot):
  changes the background colour *mid-scanline* at a precise beam position, re-synced
  every line by the HBL interrupt (from `stop`, so low jitter) to avoid the drift a
  free-running loop suffers. A clean vertical blue/red split — the Spectrum-512
  direction. Built to `disk-split/AUTO/SPLIT.PRG`; calibrate with
  `harness/intraline_split.py`.
- `megaspeed.s` — **Mega STE dual-speed demonstrator** (Phase 3, F-210): a
  VBL-locked colour hammer whose horizontal band density tracks the CPU clock —
  ~2× as many bands at 16 MHz as at 8 MHz (same beam, twice the per-scanline
  cycle budget). Launch on **Mega STE** and toggle the client's 8/16 MHz clock
  (pure B1: it relaunches with `--cpuclock`). Built to `disk-megaspeed/AUTO/MSPEED.PRG`.
- `dmasound.s` — **DMA sound + LMC1992 EQ sweep** (Phase 3, F-209): plays a
  looping sawtooth sample through the STE DMA sound hardware and continuously
  sweeps bass/treble via the Microwire interface, so a `dmatrace` capture catches
  the buffer draining and the EQ responding. Needs DMA sound + LMC1992: run on
  **STE / Mega STE**. Built to `disk-dma/AUTO/DMA.PRG`. Drives the DMA sound / EQ view.
- `blitfill.s` — **continuous blitter copies** (Phase 3, F-208): repeatedly blits
  a 16×16-word source buffer to the top-left of the screen (HOP=source,
  LOP=replace), so a `blittrace` capture always catches steady memory traffic
  (256 reads + 256 writes per blit — exercises the read path, unlike a fill).
  Needs a machine with a blitter: run on **STE / Mega ST / Mega STE**. Built to
  `disk-blit/AUTO/BLIT.PRG`. Drives the Blitter traffic view.
- `lborder.s` — **cycle-exact left-border removal**: switches the resolution
  register `$ffff8260` hi/lo at `LineCycles <= 4` on a band of scanlines, opening
  the left border there (green screen content fills the red border on those
  lines). Syncs on VBL, masks MFP interrupts to kill jitter, runs a per-line loop
  of exactly 512 cycles. Built to `disk-lborder/AUTO/LB.PRG`. The Phase 1 exit
  criterion's "border removal" case. See `docs/phase-1/left-border.md`.

Each effect has its own GEMDOS drive dir (an AUTO-folder program loops forever, so
they can't share one). Run e.g. `talos --effect tests/effects/disk-lborder`.

## Running

Effects run from the `AUTO` folder of the GEMDOS drive directory `disk/`, which
Hatari mounts with `-d tests/effects/disk`. Use a clean Hatari config so a stray
user floppy setting doesn't hijack the boot drive. See
`docs/phase-1/register-writes.md` for the full recipe and the EmuTOS quirks
(AUTO = user mode → the program calls `Super(0)`; ~10–14 s boot delay).

`disk/AUTO/RBARS.PRG` is committed as a ready-to-run fixture (106 bytes) so the
effect runs without building vasm.
