# Talos — Architecture

> **Status:** Active
> **Provenance:** Design session with Claude (primary architect/auditor); Hatari internals and prior art verified against upstream sources 2026-07-08.
> **Last reviewed:** 2026-07-11
> **Why this status:** Architecture is decided at the structural level and grounded in working prior art. Interface details (exact protocol packets, tap points) are specified as intent and must be confirmed against the current Hatari fork source during Phase 0.

---

## 1. The central choice

Tier 2 (cycle-accurate) fidelity is, functionally, a cycle-exact emulator. Writing one solo to parity with two decades of Hatari reverse-engineering is a multi-year effort concentrated entirely on undifferentiated work. Talos's value is the *visualisation*, not the core. Therefore Talos **instruments an existing cycle-exact core** rather than building one (D-002), and the core is **Hatari** (D-003).

This is not speculative. Two projects already prove the shape:

- **hrdb** — Steven Tattersall's *Hatari Remote Debugger* — is a **Qt** application that connects to a patched Hatari over a **network socket** and presents register views, disassembly, a **Hardware Window** (tree of hardware registers and state), a **Graphics Inspector** (memory interpreted as ST graphics, lockable to the video-base registers, using the Shifter palette), profiling with cycle/instruction counts, and CPU run/stop/step including **run-until-VBL and run-until-HBL**. It is built from the `debugger-extensions` branch of a Hatari fork and targets Qt5/Qt6. This is roughly 80% of Talos's plumbing, already working.
- **hatariB** — bradsmith's libretro core — surgically removed Hatari's SDL user interface and reconnected the core to the libretro interface. It proves the emulation core detaches cleanly from Hatari's own front-end.

Talos is best understood as *hrdb re-aimed*: where hrdb asks "what is the CPU doing so I can debug it," Talos asks "where on the scanline did this write land, and what did it do to the picture." Same pipe, different lens.

## 2. Process topology

Talos is **two processes** connected by a socket (D-004):

```
  +---------------------------+           remote protocol            +--------------------------+
  |   Hatari (Talos fork)     |  <------  (TCP/local socket,   ---->  |   Talos client (Qt6)     |
  |                           |           hrdb-lineage,               |                          |
  |  cycle-exact WinUAE 68000 |           extended)                   |  connection/session mgr  |
  |  GLUE/MMU video timing    |  ---- state packets (per frame /      |  state model (frame ring)|
  |  Shifter / Blitter / DMA  |        per scanline / per event) ->   |  framebuffer + overlays  |
  |  MFP timers / YM2149      |                                       |  register/timeline panels|
  |                           |  <--- control (poke reg, step,        |  machine/region switch   |
  |  + Talos instrumentation  |        run-to-HBL, load cfg) ----     |  effect workspace/export |
  |    taps (B2)              |                                       |  validation/diff harness |
  +---------------------------+                                       +--------------------------+
```

Two processes rather than one embedded core, because:

- It follows hrdb's proven boundary, so the protocol work has a reference implementation.
- It keeps the Qt6 client cleanly separated from Hatari's SDL/C world.
- It isolates crashes: a wedged emulation core does not take the UI with it.

The reversal condition (see D-004) is bandwidth: if per-cycle event volume for the heavy visualisations cannot be sustained across a socket, the fallback is single-process shared-memory embedding. This is judged unlikely at ST clock rates (8/16 MHz) but is the thing to measure in Phase 1.

## 3. Instrumentation in two layers: B1 and B2

Not all state Talos wants is equally accessible. The architecture splits the work:

**B1 — client over the existing protocol.** Everything hrdb already exposes comes essentially for free: registers, memory, hardware-register tree, VBL/HBL/cycle counters, run/stop/step, run-to-HBL/VBL, screenshots, state snapshots. Talos's Phase 0–2 visualisations are built here without touching Hatari's internals.

**B2 — fork patches plus new protocol messages.** The headline visualisations need state the debug protocol was never designed to stream: the exact cycle at which each hardware-register write landed, the beam's live position, the Blitter's per-operation memory accesses, the DMA sound buffer's drain and the LMC1992 register path. These require **instrumentation taps** inside the emulation source and **new protocol packet types** to carry the data out. Talos maintains these as patches on its Hatari fork.

The rule (D-005): **use B1 wherever it suffices; escalate to B2 per-feature only where B1 cannot supply the data.** This keeps the fork's divergence from mainline Hatari as small as possible, which matters for rebasing as Hatari improves.

### Candidate tap points (B2) — to confirm against fork source in Phase 0

| Subsystem | Approximate source area | What the tap emits |
|---|---|---|
| GLUE video timing | `video.c` (horizontal/vertical counters, border state machine, sync/res register handling) | Beam (line, cycle-in-line) each step; border-open/close events with the triggering cycle |
| Register write path | Hardware register write handlers (`ioMem*`) | `(address, value, absolute cycle, line, cycle-in-line)` for every write to a watched register |
| Blitter | `blitter.c` — **confirmed (Phase 3):** `Blitter_ReadWord` (blitter.c:431) / `Blitter_WriteWord` (blitter.c:445) are the two memory-access choke points; the marker seam is the `y_count==0` branch of `Blitter_Start` (blitter.c:907). *Implemented as `patches/0001-tap-blitter-traffic.patch`, command `blittrace`.* | Per-operation source/dest addresses and word counts; bus-cycle occupancy |
| DMA sound | `dmaSnd.c` — **confirmed (Phase 3):** drain at `DmaSnd_FIFO_Refill`, bounds at `DmaSnd_StartNewFrame`, control at `DmaSnd_SoundControl_WriteWord`, EQ at the Microwire decode switch (dmaSnd.c:1129). *Implemented as `patches/0002-tap-dma-lmc1992.patch`, command `dmatrace`.* | Frame pointer/counter, buffer fill/drain, mode register; Microwire/LMC1992 writes (master/bass/treble/balance) |
| Shifter palette | Shifter register handlers | Palette-register writes tagged with cycle, for the per-scanline palette reconstruction |
| MFP 68901 | `mfp.c` | Timer B event-count ticks on display-enable transitions (the mechanism real code uses to fire per-scanline interrupts) |

These names are indicative, from Hatari's documented `src/` layout (`cpu/` holds the WinUAE core; the machine hardware lives alongside it). Exact filenames and function seams **must be read from the fork source**, not assumed — this is the first Phase 0 task.

## 4. The framebuffer: take, don't reconstruct

Talos takes **Hatari's own rendered framebuffer** as the visual base and draws overlays on top of it (D-007), rather than reconstructing the screen from register state. Hatari's renderer is correct by construction; reconstructing it would reintroduce exactly the fidelity risk Option B was chosen to avoid. The beam overlay, the "this write happened *here*" markers, and the timeline are drawn as layers registered to the emulated screen.

A secondary "reconstruct from registers" view has teaching value (it shows the picture as the register state alone would predict, so divergence from the real frame becomes visible and instructive). If built, it is an *additional* view, never a replacement for the taken framebuffer.

## 5. The Talos client (Qt6) — component breakdown

- **Connection / session manager.** Owns the socket, launches or attaches to the Hatari fork, negotiates protocol version and capabilities (so a client can degrade gracefully when talking to a Hatari without a given B2 tap).
- **State model.** A per-frame ring of events (register writes, beam positions, Blitter/DMA activity) indexed by cycle and scanline. This is the spine every visual panel reads from.
- **Framebuffer view + overlays.** The emulated screen with the beam-position overlay and register-write markers (F-203).
- **Register-write timeline.** A cycle-axis view of the current frame showing what was written where, aligned to the scanlines and to border/display windows (F-203).
- **Register panels.** Palette (STE 4096-colour, quirk-correct — see §6), hardware scroll, Blitter control, DMA sound / Microwire / LMC1992 (F-204).
- **Machine + region switcher.** Selects 520/1040 ST, Mega ST, STE, Mega STE and PAL/NTSC; drives Hatari config and greys out panels the selected machine lacks (F-205, F-206, F-207).
- **Blitter traffic view** (F-208) and **DMA/LMC1992 view** (F-209) — the B2-fed visualisers.
- **Mega STE dual-speed demonstrator** (F-210) — toggles 8/16 MHz and shows an effect holding or breaking.
- **Effect prototyping workspace** (F-211) and **export** (F-212) — build effects interactively, emit a register sequence or asm stub, and a "verify on Hatari" round-trip.
- **Validation / diff harness** (F-213) — see §8.

## 6. Machine and region differences at the architecture level

Most variant differences are *capability presence* and reduce to config plus panel-gating. Two are genuine timing behaviours the client must represent, but both are **already emulated by Hatari** — Talos's job is to *expose and visualise* them, not to model them:

- **STE prefetch timing** differs from the ST (to support fine horizontal scroll), which moves some border-removal windows so STE border code is not always ST border code. Talos surfaces this in the beam/border overlay when the machine is STE.
- **Mega STE dual-speed** (16 MHz cache-resident, dropping to 8 MHz on ST-side bus access) makes raster cycle-counting bimodal (C-006). Hatari emulates this (documented via the *Little -ME-* demo switching to 16 MHz with the 16 KB cache); Talos visualises the two regimes and lets the user toggle between them (F-210).

The **STE palette quirk** — the least-significant intensity bit stored as the *top* bit of each nibble, kept for ST backward-compatibility — is handled in the palette panel's decode and in any shared decode crate (C-005 in `Atari_tools.md`). Getting this mapping exactly right is a small, concrete correctness test.

**PAL vs NTSC** (≈313 lines / 512 cycles-per-line at 50 Hz versus ≈263 lines / 508 at 60 Hz) changes border timings and is a first-class setting alongside machine type (F-206), passed through to Hatari config.

> Every timing constant used for overlays (cycles-per-line, border windows, prefetch offsets, wakeup-state shifts) is sourced from authoritative references — Hatari source, community ST timing documents, *Atari ST Internals* — and bench-validated. None is trusted from memory (C-008). Where possible, Talos should *read* these from Hatari rather than hard-coding them, so the client stays consistent with the core it is instrumenting.

## 7. Configuration and control

Hatari accepts configuration via `hatari.cfg` (and `--configfile` overrides), command-line options, and run-time changes through its `setopt` debugger command, a **command FIFO**, or a **control socket**. Talos uses these to set machine type, TOS, RAM, and region, and to poke registers and drive stepping. This is the same configuration surface Hermes targets for launching, so the config-generation logic is a candidate shared component between the two tools (noted, not yet decided).

State snapshots (Hatari's whole-system save/restore) let Talos capture and restore an exact machine state — useful for parking a prototype mid-effect and for seeding validation runs from a known point.

## 8. Validation

Cycle accuracy is only meaningful if tested (D-009). The harness runs an identical input — a register sequence, or a small demo — through **Talos-driven Hatari** and through **stock, unmodified Hatari**, and diffs the framebuffer **per scanline**.

Because both sides share the same emulation lineage, any divergence implicates **Talos's instrumentation or its config**, not the emulation — a far smaller search space than debugging a from-scratch emulator would present. The regression corpus is one production per hard trick (each of the four borders, Spectrum 512, sync-scroll, STE hardware scroll) and one per machine/region combination. This harness exists from Phase 1, not bolted on at the end.

## 9. Build and dependencies

- **Hatari fork:** CMake build (Hatari's native system; the `configure` wrapper is also available). Talos maintains its B2 patches as a branch, kept rebasable onto upstream. Runtime needs a TOS image (or EmuTOS) supplied by the user; none ships with Talos.
- **Talos client:** Qt6, C++ (matching hrdb's toolkit and the ecosystem's modern stack). Linux-first.
- **Shared crates (optional, later):** YM2149 decode/visual from Syrinx; palette/bitplane decode from Hephaestus F-004. Consumed as libraries; they do not pull disk-image assumptions into Talos (D-008).

## 10. Licensing (architectural consequence)

Hatari is **GPLv2**. The Talos Hatari fork and its instrumentation patches are therefore GPLv2. The Qt6 client is a separate process communicating over a socket; whether that separation places the client outside GPL's reach is a genuinely debatable licensing question and **is not relied upon** here. The safe reading is that a Talos distributed together with its patched Hatari is a GPL work. If a non-GPL result is ever required, that forces the optional from-scratch core (Phase 5, D-002 reversal / C-007). Decide licensing intent before significant B2 work, because it is expensive to reverse.
