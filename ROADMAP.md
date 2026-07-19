# Talos — Roadmap

> **Status:** Active
> **Provenance:** Design session with Claude (primary architect/auditor).
> **Last reviewed:** 2026-07-16
> **Why this status:** Phases are defined and ordered to de-risk the hardest work first (prove the pipe, then visualise, then deepen). Phases 0–4 implemented (M0–M4 reached): the pipe, the beam/register-write visualisation + harness, machine/region selection, all three hard subsystems (Blitter F-208, DMA/LMC1992 F-209, Mega STE dual-speed F-210), and the effect prototype→export→verify loop (F-211/F-212) — raster bars, intra-line vertical bands (Spectrum-512-lite, arbitrary-column) and the STE hardware scroller, each with an asm-stub + register-sequence export and a round-trip harness. Also built under F-211: a Spectrum 512 picture viewer — import a real `.SPU`/`.SPC` or convert any image to S512, decode it, and visualise the per-scanline palette storm (Talos shows the picture and its timing; it does not reproduce the effect on hardware, the tightest ST trick). Phase 5 (the from-scratch core) is untouched, by design. Phase 6 collects deeper-visualisation and interop features as an à la carte backlog (timeline scrubber, border walkthrough, F-218, sync-scroll, more image formats, …) — not on any critical path.

---

The ordering principle is **prove the thinnest end-to-end path first, then widen**. Phase 0 exists to retire the single biggest unknown — that the remote-client-on-Hatari pipe works at all for Talos's purposes — before any visualisation is built. The validation harness (Phase-level cross-cutting) comes online in Phase 1 and grows with every subsequent phase.

Each phase lists its **goal**, the **B1/B2 split** (client-over-protocol versus fork-patch work), and its **exit criterion** — the concrete thing that must be true to move on.

## Phase 0 — Groundwork: prove the pipe

**Goal.** Stand up the two-process skeleton end to end with no visualisation yet.

- Clone and build the Hatari fork carrying the `debugger-extensions`-lineage remote protocol (the one hrdb uses). Confirm it builds on the Linux target with CMake.
- Read the actual `src/` layout and confirm the candidate B2 tap points in `ARCHITECTURE.md §3` against real source — filenames, functions, where the video/Blitter/DMA/register-write seams actually are. **This is the first task and it gates the B2 estimates.**
- Build a minimal Qt6 client that connects over the socket, reads registers plus VBL/HBL/cycle counters, issues run/stop/step, and displays Hatari's framebuffer in a Qt window.

**B1/B2.** Pure B1. No fork patches yet.

**Exit criterion.** The Qt6 client shows Hatari's live screen, reports the current cycle/scanline, and can single-step — all over the socket. The B2 tap points are confirmed in source and the fork's divergence from mainline is understood.

## Phase 1 — Beam and register visualisation (the core value, proven once)

**Goal.** Demonstrate the defining feature on exactly one effect: show *where on the scanline* a register write lands, for one border-removal effect, end to end.

- Beam-position overlay on the taken framebuffer.
- Register-write-to-cycle mapping and the cycle-axis timeline for the current frame.
- Bring the **validation harness** online: per-scanline framebuffer diff against stock Hatari for this one effect.

**B1/B2.** B1 if the existing protocol can stream per-write cycle data at sufficient granularity; escalate to the **register-write tap** (B2) if it cannot. This phase is where the socket-bandwidth reversal condition (D-004) is measured.

**Exit criterion.** Loading a known left-border-removal routine, Talos shows the sync/resolution write landing at its exact cycle and the border opening as a consequence, and the diff harness confirms Talos's driven run matches stock Hatari to the scanline.

## Phase 2 — Machine and region: the differential view

**Goal.** Make the four machines and two regions first-class, and turn their differences into a teaching feature.

- Machine selector (520/1040 ST, Mega ST, STE, Mega STE) and region selector (PAL/NTSC), driving Hatari config.
- Capability gating: panels grey out for absent hardware (no Blitter on plain ST, no scroll/DMA on ST, etc.).
- The **differential view**: flip ST↔STE and watch the palette collapse 4096→512, the scroll register die, Blitter and DMA panels disappear.

**B1/B2.** Mostly B1 (config + protocol capability negotiation). The differential view is client-side.

**Exit criterion.** Switching machine/region reconfigures the emulated machine correctly and the UI honestly reflects each machine's capabilities; the ST↔STE differential is legible at a glance.

## Phase 3 — The hard subsystems, visualised

**Goal.** The visualisations that need deep instrumentation.

- **Blitter memory-traffic view** — per-operation source/dest access, bus occupancy (Blitter tap).
- **DMA sound buffer-drain + LMC1992 EQ-curve view** — buffer fill/drain, mode register, Microwire path to master/bass/treble/balance (DMA tap).
- **Mega STE dual-speed demonstrator** — toggle 8/16 MHz and watch a raster effect hold or break, making the bimodal cycle budget (C-006) visible.

**B1/B2.** Predominantly B2: this phase is where most fork patches and new protocol packets land.

**Exit criterion.** A Blitter fill is watchable as memory traffic; a DMA sample is watchable draining with the EQ curve responding; the Mega STE speed toggle visibly changes whether an effect fits its scanline budget.

## Phase 4 — Effect prototyping and export

**Goal.** Close the loop from "poke and watch" to "reproduce on real hardware."

- Effect prototyping workspace: build a raster bar, a Spectrum 512 image, a hardware scroller interactively.
- Export the register sequence, and/or generate a small asm stub that reproduces the effect.
- "Verify on Hatari" round-trip: run the exported artefact back through stock Hatari and confirm it reproduces (reusing the diff harness).

**B1/B2.** B1 for driving; client-side for the workspace and codegen.

**Exit criterion.** An effect built in Talos exports to an asm stub that, assembled and run in stock Hatari, reproduces the effect — verified by the harness.

## Phase 5 — (Optional, gated) from-scratch core

**Goal.** Own the bottom layer — only if there is a reason to.

Swap a bespoke cycle-exact core in behind the **same client protocol**, so the client is unaffected. Undertaken only if (a) ownership of the emulation core becomes a goal in its own right, or (b) a **non-GPL** result becomes a hard requirement (C-007). This is a multi-year effort in its own right and is explicitly *not* on the critical path.

**Exit criterion.** N/A unless triggered. If triggered, parity with Hatari on the regression corpus is the bar.

## Phase 6 — Deeper visualisation & interop (à la carte backlog)

**Goal.** Widen *what* Talos makes visible and *how* effects get in and out, without touching the Phase-5 core question. These are independent items — not a strict order — prioritised by how directly they serve the mission ("show the timing", never emulate; D-002). Schedule them individually; each earns an F-NNN entry in `REGISTERS.md` when it is actually taken up, and round-trips through the harness on its own.

**Tier 1 — closest to the mission (some already flagged):**

- **Per-frame timeline scrubber.** ✓ **Built.** Drag a cursor through one frame; every register write lights up at its scanline+cycle and the screen rebuilds up to that point. Extends the existing register-write capture + beam overlay.
- **Border-removal walkthrough.** ✓ **Built.** A guided view of each of the four ST borders (left/right/top/bottom): a 2-D screen diagram (X = cycles, Y = lines) showing the display rectangle, the region the trick opens, and where/when the sync/resolution write must land — every figure sourced from Hatari `video.h`/`video.c` (C-007). The **left border** is runnable (Build & Run opens it live; Verify confirms via `diff_harness.py --border-check`); the other three are teaching views with the exact write, window and consequence. (The bottom-border freq-switch would not open cycle-exact across a full in-sandbox delay sweep, so it ships as a teaching view rather than an unverified runnable.)
- **F-218 — reconstruct-from-registers view.** ✓ **Built.** A second screen rebuilt *from the captured palette writes* (folded onto one frame by beam position, coloured by the ST/STE palette decode), shown beside Hatari's taken framebuffer so you see *why* the picture looks as it does — and where the register field and reality diverge. Meaningful for palette-register captures ($ff8240 reconstructs a raster/band background field). Per D-007 it is secondary and never replaces F-202.
- **Sync-scroll.** ✓ **Built (visualise).** A guided view of the STF sync-scroll trick — ST Connexion's three `$ffff8260` switches per line (hi → med → lo), where the *exact* low-res cycle sets the shift (16→0, 20→13, 24→9, 28→5, 32→1 px) — with a line-start switch timeline, the sourced cycle→pixel table, and a before/after illustration. Sourced from Hatari `video.h`/`video.c` (C-007). Shipped as a teaching view: the shift needs an *exact* per-cycle landing (not a window), which would not converge cycle-exact across an in-sandbox tune, and there is no bench-proven stub for it (unlike the runnable left border) — so the mechanism is documented, not reproduced live.

**Tier 2 — natural extensions of what's built:**

- **More ST image formats** — ✓ **Built.** Degas (`.PI1/.PC1`), NEOchrome (`.NEO`), Tiny (`.TNY`) — reusing the Spectrum 512 palette/decode plumbing (F-211).
- **Live disassembly synced to the beam** — ✓ **Built.** The *Disassembly* tab traces the next N instructions from PC (break, then single-step), tabulating each with Hatari's own disassembly, its beam position (scanline + cycle) and its cycle cost — read from Hatari, never estimated (D-002). Video-register writes (`$ff82xx`) are highlighted, and selecting a row parks the beam overlay there. Pure B1 (break/step/regs + `console disasm` over the redirect file); re-aims hrdb's raw pieces at "this write happens here".
- **MFP timer / interrupt visualisation** — Timer A–D configuration and when each interrupt fires on the beam (Timer-B drives Spectrum 512, HBL drives scrollers).
- **Per-scanline cycle-budget meter** — ✓ **Built.** Cycles used vs the ~512-per-line budget for an authored effect, and where it overflows (ties to the Mega STE dual-speed view, F-210).

**Tier 3 — presentation & interop:**

- **Frame / GIF recorder** — ✓ **Built.** Capture the (now tear-free, BUG-009) live view to a looping animated GIF for sharing an effect — a self-contained GIF89a encoder (ST content is palette-indexed, so colours are preserved exactly).
- **A/B machine comparison** — the same effect on two machines/regions side by side, highlighting where the STE prefetch shift breaks it (extends the F-207 differential).
- **Custom font import for the scroller** — ✓ **Built.** Bring-your-own font (from a font-sheet image) instead of the built-in 8×8 (F-212 scroller).

**Housekeeping already on the books:**

- **F-216** — Hatari configuration generation, shareable in concept with Hermes (decide when Hermes work begins).
- **Regression-corpus runner** — one command that runs the whole corpus and reports per-scanline diffs against stock Hatari (extends `harness/diff_harness.py`).

**B1/B2.** Mostly B1 + client-side. Live disassembly and the MFP/timer taps may need a B2 tap confirmed against source first — justify each one and keep it surgical (C-002/C-003).

**Exit criterion.** None as a whole — this is an à la carte backlog; each item ships, earns its F-NNN, and is harness-checked on its own.

---

## Cross-cutting: the validation harness

From Phase 1 onward, every phase extends the per-scanline diff corpus:

- Phase 1: one border-removal effect.
- Phase 2: one production per machine/region combination.
- Phase 3: Blitter, DMA, and dual-speed cases.
- Phase 4: every exported effect round-trips through the harness by construction.

The harness is not a final QA step; it is the continuous check that Talos's instrumentation still agrees with the core it instruments.

## Suggested milestone tags

- **M0** — pipe proven (end of Phase 0). ✓
- **M1** — invisible made visible on one effect, harness live (end of Phase 1). ✓
- **M2** — all machines/regions, differential view (end of Phase 2). ✓
- **M3** — hard subsystems visualised (end of Phase 3). ✓
- **M4** — prototype-and-export loop closed (end of Phase 4). ✓
