# Talos — Validation / diff harness (F-213)

> **Status:** Active
> **Provenance:** Claude (implementer), Phase 0 scaffold.
> **Last reviewed:** 2026-07-09
> **Why this status:** Directory reserved. The harness comes online in Phase 1 (D-009), not Phase 0.

---

Runs an identical input — a register sequence or a small demo — through
**Talos-driven Hatari** and through **stock, unmodified Hatari**, and diffs the
framebuffer **per scanline** (D-009).

Because both sides share the same emulation lineage, any divergence implicates
**Talos's instrumentation or config**, never the emulation — a small search space.

Grows with every phase (ROADMAP cross-cutting section):

- Phase 1: one border-removal effect.
- Phase 2: one production per machine/region combination.
- Phase 3: Blitter, DMA, dual-speed cases.
- Phase 4: every exported effect round-trips by construction.

Built when `-DTALOS_BUILD_HARNESS=ON` (off by default in the top-level build).
