# Talos — Handoff (CLAUDE.md)

> **Status:** Active
> **Provenance:** Design session with Claude (primary architect/auditor); Hatari facts verified against upstream sources 2026-07-08.
> **Last reviewed:** 2026-07-08
> **Why this status:** Design complete and grounded; implementation not started. This document is the cold-start brief for whoever (AI or human) picks Talos up next.

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
5. `ROADMAP.md` — the six phases; start at Phase 0.

## Prior art you must look at before writing code

- **hrdb** (Steven Tattersall) — a Qt debugger client on a patched Hatari's remote socket. It already has a Hardware Window, a Graphics Inspector, register/cycle/VBL/HBL views, and run-to-HBL/VBL stepping. Talos is *this pipe, re-aimed at visualisation*. Study its structure and its protocol before designing ours.
- **hatariB** (bradsmith) — a libretro core that stripped Hatari's SDL UI and reconnected the core. Proof the core detaches cleanly; useful if the D-004 reversal (single-process embedding) is ever triggered.

## First tasks (Phase 0, in order)

1. Clone and build the Hatari fork carrying the hrdb-lineage `debugger-extensions` remote protocol on the Linux target (CMake). Confirm it runs and the socket connects.
2. **Read the real `src/` layout** and confirm the candidate B2 tap points in `ARCHITECTURE.md §3` against actual source (`video.c`, `blitter.c`, `dmaSnd.c`, the `ioMem*` register handlers, `mfp.c`, the WinUAE core in `cpu/`). Update `ARCHITECTURE.md` §3 with the real filenames/functions and record any surprises. This gates every B2 estimate — do it before promising Phase 3 timelines. *(C-003.)*
3. Minimal Qt6 client: connect, read registers + cycle/scanline counters, run/stop/step, display Hatari's framebuffer. That is M0.

## The traps, stated plainly

- **Socket bandwidth (C-004).** Per-cycle event streams may or may not fit over a socket. Measure it in Phase 1. If it doesn't fit, the answer is single-process shared-memory embedding (D-004 reversal), *not* thinning the data until the visualisation lies.
- **Fork divergence (C-002).** Every B2 patch widens the gap from mainline Hatari and the rebase cost. Justify each one by a specific feature B1 can't serve. Keep patches surgical.
- **Mega STE dual-speed (C-005).** 16 MHz cache-resident, 8 MHz on ST-side bus access. Cycle-counting is bimodal; represent both regimes, never an average. This is a *feature* to visualise (F-210), not a nuisance to smooth over.
- **STE palette quirk (C-008).** LSB of each intensity nibble is stored as the top bit. Get it exactly right; it is a concrete unit test.
- **Timing constants (C-007).** Never trust a cycles-per-line or border-window figure from memory. Source it, bench-validate it, and prefer reading it from Hatari over hard-coding.
- **Licensing (C-006 / D-010).** GPLv2 all the way through. If anyone ever needs Talos non-GPL, that is not a tweak — it forces the from-scratch core (Phase 5). Settle intent early.

## What "done" looks like at each milestone

- **M0:** Qt6 client shows Hatari's live screen, current cycle/scanline, single-step — over the socket.
- **M1:** one border-removal effect shows its triggering write landing at the exact cycle, with the diff harness confirming Talos-driven Hatari matches stock Hatari per scanline.
- **M2:** all four machines + PAL/NTSC selectable with honest capability gating; ST↔STE differential legible.
- **M3:** Blitter traffic, DMA drain + LMC1992 EQ, and Mega STE dual-speed all watchable.
- **M4:** an effect built in Talos exports to an asm stub that reproduces in stock Hatari, verified by the harness.

## Repo hygiene (house standard)

- Four-field blockquote header on every document: Status / Provenance / Last reviewed / Why this status. Status vocabulary: Active, Dormant, Complete, Archived, Superseded.
- Append-only registers: F-NNN features, D-NNN decisions, C-NNN constraints. Never renumber; never delete — supersede.
- British English. ISO 8601 dates. Provenance uses role tags (e.g. "Claude (primary auditor)").
- Decisions always carry a reversal condition. Propose Status/Why changes for confirmation rather than committing silently.

## Open threads to resolve as you go

- Exact remote-protocol packet set for the B2 taps (define once tap points are confirmed in Phase 0).
- Whether Hatari config generation (F-216) becomes a component genuinely shared with Hermes or stays a private copy — decide when Hermes work begins.
- Whether the secondary reconstruct-from-registers view (F-218) earns its place, or stays a note.
