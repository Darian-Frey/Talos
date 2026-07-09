# Talos — Decisions

> **Status:** Active
> **Provenance:** Design session with Claude (primary architect/auditor); Hatari prior art verified 2026-07-08.
> **Last reviewed:** 2026-07-08
> **Why this status:** The load-bearing decisions are made and grounded in working prior art. Interface-level specifics remain to be confirmed against fork source in Phase 0.

---

Append-only. Talos-project-scoped (D-001–). These inherit from the ecosystem-level decisions in `Atari_tools.md` (whose Talos-relevant entries were D-003 and D-007); where an ecosystem decision is the parent, it is cited. Each decision pairs with the condition under which it should be revisited.

**D-001 — Talos targets Tier 2 (cycle-accurate) fidelity across all four machines (520/1040 ST, Mega ST, STE, Mega STE) and both regions (PAL/NTSC).**
Parent: `Atari_tools.md` D-007.
Rationale: the platform-defining effects (border removal, Spectrum 512, sync-scroll, STE hardware scroll) are emergent from exact internal timing; there is no lower tier that still shows them.
Reversal condition: none at the product level — a lower tier would defeat the purpose. If effort proves unsustainable, the response is to narrow *machine coverage* or *effect scope*, not to drop the tier.

**D-002 — Build by instrumenting an existing cycle-exact core (Option B), not by writing one (Option A).**
Parent: `Atari_tools.md` D-007.
Rationale: the core is undifferentiated heavy lifting already solved; Talos's value is the visualisation layer. Option B is faster, more accurate, and makes validation tractable.
Reversal condition: adopt Option A only via Phase 5, if ownership of the core becomes a goal in itself or a non-GPL result is required (C-007). Any such core sits behind the same client protocol so the client is unaffected.

**D-003 — The core is Hatari, and specifically a fork carrying the `debugger-extensions`-lineage remote protocol used by `hrdb`.**
Rationale: Hatari is the cycle-exact reference (WinUAE 68000, all four machines, TOS, PAL/NTSC, the four STF wakeup states, accurate border removal). The hrdb fork already exposes a Qt-friendly remote protocol with register/memory/hardware/graphics views and cycle/VBL/HBL stepping — roughly 80% of Talos's plumbing, proven.
Reversal condition: if the fork becomes unmaintained or diverges from mainline Hatari in ways that block needed machine accuracy, rebase onto mainline Hatari and port the protocol/taps forward.

**D-004 — Two-process architecture: Hatari as one process, the Talos Qt6 client as another, connected by a socket.**
Rationale: mirrors hrdb's proven boundary; isolates the Qt6/C++ client from Hatari's SDL/C world; isolates crashes.
Reversal condition: if per-cycle event volume for the heavy visualisations cannot be sustained over a socket at ST clock rates, move to single-process shared-memory embedding. Measure this in Phase 1.

**D-005 — Layered instrumentation: B1 (client over the existing protocol) wherever it suffices; B2 (fork patches + new protocol packets) only per-feature where B1 cannot supply the data.**
Rationale: minimises the fork's divergence from mainline, keeping it rebasable as Hatari improves.
Reversal condition: none; the policy is applied per-feature. If B1 turns out to cover more than expected, fewer B2 patches are needed — a good outcome, not a reversal.

**D-006 — The Talos client is Qt6 / C++.**
Rationale: matches hrdb's toolkit (so its structure is a reference), matches the ecosystem's modern stack, and is the natural fit for a rich desktop visual tool.
Reversal condition: none foreseen.

**D-007 — Take Hatari's rendered framebuffer as the visual base and overlay on it; do not reconstruct the screen from register state as the primary view.**
Rationale: Hatari's renderer is correct by construction; reconstructing it would reintroduce the fidelity risk Option B exists to avoid.
Reversal condition: a "reconstruct from registers" mode may be added as a *secondary, additional* teaching view (it makes register-vs-reality divergence visible), but never as a replacement for the taken framebuffer.

**D-008 — Talos is a separate application; it shares only low-level crates (YM2149 from Syrinx, palette/bitplane decode from Hephaestus F-004), not the workbench itself.**
Parent: `Atari_tools.md` D-003 (refined).
Rationale: Talos is not a disk tool and must not inherit disk-image assumptions; but general DSP/decode libraries are legitimately shared.
Reversal condition: revisit crate-sharing only if a shared crate accrues ST-disk-specific assumptions that do not fit a pure-hardware consumer.

**D-009 — Validation is a per-scanline framebuffer diff against stock, unmodified Hatari, built in from Phase 1.**
Rationale: shared lineage means any divergence implicates Talos's instrumentation/config, not the emulation — a small, tractable search space. Continuous checking, not end-stage QA.
Reversal condition: none; if anything, broaden the corpus over time.

**D-010 — GPLv2 is accepted for the Hatari fork and its patches; a Talos distributed with its patched Hatari is treated as a GPL work.**
Parent: `Atari_tools.md` C-007.
Rationale: Option B on a GPL core inherits GPL; relying on the socket boundary to escape GPL is legally debatable and is not relied upon.
Reversal condition: a hard non-GPL requirement forces Phase 5 (from-scratch core). Decide licensing intent before significant B2 work; it is expensive to reverse.
