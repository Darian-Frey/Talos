# Talos — Handoff (CLAUDE.md)

> **Status:** Active
> **Provenance:** Design session with Claude (primary architect/auditor); implementation by Claude (implementer) through Phase 4. Hatari facts verified against upstream sources 2026-07-08.
> **Last reviewed:** 2026-07-15
> **Why this status:** Client implemented through Phase 4 — M0–M4 realised (live client, machine/region gating, B2 Blitter/DMA/dual-speed views, effect authoring → export → verify incl. the STE hardware scroller), plus F-217 state snapshots. Phase 5 (the from-scratch non-GPL core) is untouched, by design. This document remains the cold-start brief for whoever (AI or human) picks Talos up next.

---

## What Talos is, in one line

A Qt6 **visualiser** that drives a patched **Hatari** over a remote socket to make ST/STE hardware timing **visible** — where on the scanline each register write lands and why an effect works — across all four machines and both video regions.

## The one thing to internalise first

**Talos does not emulate anything.** It instruments Hatari, which does the emulation. Every question of the form "how do we make the border removal cycle-exact" is answered by "Hatari already does; our job is to *show* it." If you find yourself designing emulation logic, stop — that is out of scope by decision (D-002), and the correctness risk it reintroduces is exactly what the whole architecture avoids.

## Read in this order

1. `README.md` — what and why.
2. `ARCHITECTURE.md` — the two-process design, the B1/B2 instrumentation split, the tap points, the data flow. This is the important one.
3. `DECISIONS.md` — the ten decisions and why each is the way it is. Do not silently reverse any; each has an explicit reversal condition.
4. `REGISTERS.md` — features (F-201–) and constraints (C-001–).
5. `ROADMAP.md` — the six phases (Phases 0–4 are substantially built; see "Where things stand" below).
6. `BUGS.md` — open known issues and by-design limitations (BUG-NNN); check before you trip over one.

## Prior art you must look at before writing code

- **hrdb** (Steven Tattersall) — a Qt debugger client on a patched Hatari's remote socket. It already has a Hardware Window, a Graphics Inspector, register/cycle/VBL/HBL views, and run-to-HBL/VBL stepping. Talos is *this pipe, re-aimed at visualisation*. Study its structure and its protocol before designing ours.
- **hatariB** (bradsmith) — a libretro core that stripped Hatari's SDL UI and reconnected the core. Proof the core detaches cleanly; useful if the D-004 reversal (single-process embedding) is ever triggered.

## Where things stand (2026-07-15)

Phase 0 is done: the Hatari fork (hrdb-lineage `debugger-extensions` protocol, TCP 56001, proto 0x1007) builds and connects, and the M0 client is live. The `client/` app now covers, roughly by phase:

- **M0** — connect/attach, register + cycle/scanline counters, run/stop/step, live framebuffer with a beam overlay. Beam geometry handles low/med/mono res and PAL/NTSC (`BeamGeometry`), reading the actual region from `$ff820a`.
- **M2** — machine selector (520/1040 ST, Mega ST, STE, Mega STE), region + language, honest capability gating and the ST↔STE differential (F-205/206/207). Palette view with the STE 4096-colour quirk (C-008).
- **M3** — B2 trace views: Blitter memory traffic (F-208), DMA sound drain + LMC1992 EQ (F-209), Mega STE 8/16 MHz dual-speed toggle (F-210, scoped per C-005/BUG-008).
- **M4** — effect authoring workspaces (F-211) with codegen → vasm → run and export / verify / import (F-212): raster bars, vertical bands (Spectrum-512-lite, arbitrary-column via a bench-validated calibration), and the **STE hardware fine-scroll scroller**. Click-to-place authoring from the framebuffer.
- Plus **F-217** whole-system state snapshots and a fast-boot toggle (BUG-007).

Harnesses in `harness/`: `diff_harness.py` (per-scanline determinism/non-perturbation), `raster_roundtrip.py`, `intraline_split.py`, `scroller_scroll.py`. Fixed defects and by-design limits are in `BUGS.md` (through BUG-009). **Read `git log` and `REGISTERS.md`/`BUGS.md` for the authoritative current state before assuming a feature is or isn't present.**

### Where to pick up

- Still open by design/decision: **Phase 5** (from-scratch non-GPL core; only if C-006/D-010 forces it), **F-216** (whether Hatari config-gen is shared with Hermes), **F-218** (the reconstruct-from-registers teaching view).
- B2 tap points are confirmed in source where features needed them (`video.c` for STE scroll/region, the Blitter and DMA-sound paths for F-208/209); `ARCHITECTURE.md §3` has not been swept end-to-end against source, so treat any *unbuilt* B2 estimate as provisional (C-003).
- Two Hatari measurement gotchas any new headless/live capture must respect (learned the hard way, see BUG-009 and `scroller_scroll.py`): fast-forward **frame-skips rendering** (use `--frameskips 0`, or grab coherently), and its VBL breakpoint count **races past boot** (target a VBL relative to / above the live count).

## The traps, stated plainly

- **Socket bandwidth (C-004).** Per-cycle event streams may or may not fit over a socket. Measure it in Phase 1. If it doesn't fit, the answer is single-process shared-memory embedding (D-004 reversal), *not* thinning the data until the visualisation lies.
- **Fork divergence (C-002).** Every B2 patch widens the gap from mainline Hatari and the rebase cost. Justify each one by a specific feature B1 can't serve. Keep patches surgical.
- **Mega STE dual-speed (C-005).** 16 MHz cache-resident, 8 MHz on ST-side bus access. Cycle-counting is bimodal; represent both regimes, never an average. This is a *feature* to visualise (F-210), not a nuisance to smooth over.
- **STE palette quirk (C-008).** LSB of each intensity nibble is stored as the top bit. Get it exactly right; it is a concrete unit test.
- **Timing constants (C-007).** Never trust a cycles-per-line or border-window figure from memory. Source it, bench-validate it, and prefer reading it from Hatari over hard-coding.
- **Licensing (C-006 / D-010).** GPLv2 all the way through. If anyone ever needs Talos non-GPL, that is not a tweak — it forces the from-scratch core (Phase 5). Settle intent early.

## What "done" looks like at each milestone

Status as of 2026-07-15 — `git log` / `REGISTERS.md` are authoritative if this drifts.

- **M0 ✓:** Qt6 client shows Hatari's live screen, current cycle/scanline, single-step — over the socket.
- **M1 ✓ (largely):** the beam/border overlay lands register writes on the scanline+cycle, and the authoring→export→verify loop + per-scanline `diff_harness.py` confirm Talos-driven Hatari matches stock Hatari. A dedicated single border-removal walkthrough is covered by the overlay rather than a bespoke demo.
- **M2 ✓:** all four machines + PAL/NTSC selectable with honest capability gating; ST↔STE differential legible.
- **M3 ✓:** Blitter traffic, DMA drain + LMC1992 EQ, and Mega STE dual-speed all watchable (F-208/209/210).
- **M4 ✓:** effects built in Talos (raster bars, vertical bands, STE scroller) export to an asm stub that reproduces in stock Hatari, verified by the harnesses.

## Repo hygiene (house standard)

- Four-field blockquote header on every document: Status / Provenance / Last reviewed / Why this status. Status vocabulary: Active, Dormant, Complete, Archived, Superseded.
- Append-only registers: F-NNN features, D-NNN decisions, C-NNN constraints, BUG-NNN known issues. Never renumber; never delete — supersede.
- British English. ISO 8601 dates. Provenance uses role tags (e.g. "Claude (primary auditor)").
- Decisions always carry a reversal condition. Propose Status/Why changes for confirmation rather than committing silently.

## Open threads to resolve as you go

- Exact remote-protocol packet set for any *further* B2 taps — the built views (F-208/209/210) settled their own packet needs; a general per-cycle event stream is still undefined and unproven over the socket (C-004; so far the screenshot-poll + register reads have sufficed for the live view, and BUG-009 shows coherent capture is the live-view constraint, not raw bandwidth).
- Whether Hatari config generation (F-216) becomes a component genuinely shared with Hermes or stays a private copy — decide when Hermes work begins.
- Whether the secondary reconstruct-from-registers view (F-218) earns its place, or stays a note.
- Whether to sweep `ARCHITECTURE.md §3` tap points against source end-to-end (C-003), now that several are confirmed piecemeal.
