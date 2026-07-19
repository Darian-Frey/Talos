# Talos — B2 fork patches

> **Status:** Active
> **Provenance:** Claude (implementer), Phase 0 scaffold.
> **Last reviewed:** 2026-07-19
> **Why this status:** First B2 patch landed in Phase 3 (`0001`, the Blitter tap). Further patches added only where a feature proves B1 cannot serve it (D-005) — `0003` (runtime floppy swap) is the newest.

---

The **only** part of the Hatari fork tracked in this repo. The fork itself is
cloned into `external/` and never vendored (GPLv2 upstream, separate build).

Each patch here is an instrumentation **tap** (ARCHITECTURE §3) plus the new
protocol packet that carries its data out. Rules:

- **One patch per feature**, justified by a specific visualisation B1 cannot
  supply (C-002). Name them so the feature is obvious, e.g.
  `0001-tap-register-write-cycle.patch`.
- **Keep them surgical and rebasable** — every patch widens the gap from
  mainline and the rebase cost (C-002). Minimise the seam touched.
- **Confirm tap points against real source first** (C-003) — the candidate
  points in ARCHITECTURE §3 are indicative until read in the fork.

Maintained as an ordered series applied on top of the pinned fork commit by
`scripts/bootstrap-hatari.sh` (idempotent: it skips already-applied patches).

## The series

- **`0001-tap-blitter-traffic.patch`** (F-208) — opt-in blitter memory-traffic
  trace. Records each bus access (`Blitter_ReadWord` / `Blitter_WriteWord`) and a
  blit-complete marker (`Blitter_Start`, `y_count==0`) into a host-side ring
  buffer; exposes it over the new `blittrace` command (protocol/b1-protocol.md).
  Off by default, so it does not perturb emulation. 3 files, +123 lines.
- **`0002-tap-dma-lmc1992.patch`** (F-209) — opt-in DMA-sound + LMC1992 EQ trace.
  Records sample-buffer drain, frame bounds, control/mode and decoded EQ setting
  changes; `dmatrace on` snapshots current state so a capture is self-contained.
  Exposed over the `dmatrace` command. Off by default. 3 files, +146 lines.

Both rings hold up to 65536 entries; the `on [N]` argument caps a capture below
that (the client's Depth control), and the client flags a truncated capture.

- **`0003-remote-floppy-swap.patch`** (F-219) — the one non-tap patch: a control
  command, not a data stream. Adds `floppy <0|1> <path|none>` to the remote
  command list (`Floppy_SetDiskFileName` + `Floppy_InsertDiskIntoDrive`, which
  raises the FDC media-change), so a **multi-disk demo can be fed its next disk at
  runtime without a reboot**. B1 has no way to change a floppy while running
  (D-005), so this is justified; it is surgical (one command, 1 file, +~45 lines).
