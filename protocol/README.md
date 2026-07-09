# Talos — Remote protocol

> **Status:** Active
> **Provenance:** Claude (implementer), Phase 0 scaffold.
> **Last reviewed:** 2026-07-09
> **Why this status:** Placeholder for the protocol contract. The B1 (existing) surface is discovered from fork source in Phase 0; B2 packet types are defined per-feature as taps are added.

---

The single source of truth for the wire contract between the Talos client and the
Hatari fork. Two layers, per D-005:

- **B1** — the existing hrdb-lineage `debugger-extensions` protocol: registers,
  memory, hardware-register tree, VBL/HBL/cycle counters, run/stop/step,
  run-to-HBL/VBL, screenshots, state snapshots. Talos consumes this as-is.
  **Phase 0 task:** read the fork's protocol implementation and document the
  actual message set here (this file currently asserts nothing verified).

- **B2** — new packet types Talos adds for state the debug protocol never
  streamed: per-cycle register-write `(address, value, cycle, line, cycle-in-line)`,
  live beam position, Blitter per-op traffic, DMA-sound drain + LMC1992 path.
  Each is justified by a specific feature B1 cannot serve (C-002) and defined
  here alongside the tap that emits it (ARCHITECTURE §3).

Keep this language-agnostic: it is the contract, not the client's implementation
of it (that lives in `client/src/protocol/`).
