# Test effects

> **Status:** Active
> **Provenance:** Claude (implementer), Phase 1.
> **Last reviewed:** 2026-07-10
> **Why this status:** First effect (rasterbars) built and verified driving the B1 register-write capture. More (border removal, etc.) come as harder harness cases.

---

Small ST programs that write hardware registers per frame, to drive Talos's
register-write-to-cycle capture (and, later, the validation harness). Assembled
with vasm (`scripts/bootstrap-vasm.sh`, then `scripts/build-effects.sh`).

- `rasterbars.s` — writes the background-colour register `$ffff8240` across the
  frame (rolling colour bands + writes spread over the beam). Built to
  `disk/AUTO/RBARS.PRG`.
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
