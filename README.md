# Talos

> **Status:** Active
> **Provenance:** Design session with Claude (primary architect/auditor); implementation by Claude (implementer) through Phase 4. Hatari facts verified against upstream documentation and the tattlemuss/bradsmith forks on 2026-07-08.
> **Last reviewed:** 2026-07-16
> **Why this status:** The architecture (Option B, remote-client-on-Hatari) is decided, grounded in working prior art (hrdb), and **built** — the client is implemented through Phase 4 (M0–M4: live client, machine/region gating, Blitter/DMA/dual-speed views, and the effect prototype→export→verify loop incl. the STE hardware scroller), plus F-217 state snapshots. Phase 5 (the from-scratch core) is untouched, by design. See `manual.md` to use it, `CLAUDE.md`/`ROADMAP.md` for state, `git log` for the authoritative detail.

---

Talos is a **cycle-accurate Atari ST/STE hardware sandbox and visualiser**. Its purpose is not to run software — Hatari and Hermes already do that — but to **make the invisible visible**: to show *where on the scanline* a hardware-register write takes effect and *why* an effect works, across the whole ST family (520/1040 ST, Mega ST, STE, Mega STE) and both video regions (PAL/NTSC).

It is named for the bronze automaton of Crete — a made machine that guards and demonstrates. The name fits: Talos exposes the machine's own workings rather than hiding them behind a running program.

## What it is for

- **Making the invisible visible** — beam-position overlays, register-write-to-cycle mapping, Blitter memory traffic, DMA sound buffer drain and the LMC1992 EQ curve. This is the value Hatari cannot give and the reason Talos exists.
- **Teaching the hardware interactively** — poke a register, see the result immediately, understand the cause.
- **Prototyping demo effects** — build a raster bar, a Spectrum 512 image, or a hardware scroller without the assemble–link–run loop, then export the register sequence or an asm stub.
- **Demonstrating the machine differences** — flip ST↔STE and watch the palette collapse 4096→512, the scroll register die, and the Blitter and DMA panels grey out.

## How it works, in one paragraph

Talos does **not** contain an emulation core. It is a **Qt6 client** that drives a patched build of **Hatari** — the cycle-exact ST/STE/TT/Falcon emulator — over a **remote socket protocol**, exactly as Steven Tattersall's `hrdb` debugger already does. Hatari supplies the cycle-exact 68000 (WinUAE lineage), all four machine variants, TOS handling, PAL/NTSC timing, and the notoriously hard GLUE/MMU wakeup-state and border-removal edge cases. Talos supplies the interaction and the visualisation. Where the existing remote protocol cannot expose a piece of internal state Talos needs (per-cycle register writes, Blitter/DMA internal traffic), Talos adds instrumentation taps to its Hatari fork and new protocol messages to carry them. See `ARCHITECTURE.md`.

## Document map

| Document | Purpose |
|---|---|
| `README.md` | This file — what Talos is and where to start. |
| `manual.md` | User manual — set-up, every control, and the authoring/verify workflows. |
| `ARCHITECTURE.md` | The two-process design, the Hatari fork and remote protocol, the B1/B2 instrumentation layering, the subsystem taps, and the data flow. |
| `ROADMAP.md` | The six-phase build plan (0–5), keyed to the architecture, with the validation harness as a cross-cutting concern. |
| `DECISIONS.md` | Append-only decision register (D-NNN, Talos-scoped), each with a reversal condition. |
| `REGISTERS.md` | Append-only feature (F-NNN) and constraint (C-NNN) registers. |
| `BUGS.md` | Append-only register of open known issues and by-design limitations (BUG-NNN). |
| `CLAUDE.md` | Handoff document for an AI or human collaborator picking this up cold. |

## Relationship to the wider ecosystem

Talos is one of three new tools scoped in `Atari_tools.md`, alongside Hephaestus (content workbench) and Hermes (Hatari launch manager). Talos stands apart from the disk/catalogue data flow — it touches no images and no ManifeST fingerprints — but it shares two things: the **verify-on-Hatari** round-trip idea with Hermes' lane, and, as *crates only*, the YM2149 core (from Syrinx) and the palette/bitplane-decode substrate (Hephaestus F-004). The application boundary holds; only low-level libraries cross it.

## Status of the hard parts

- **Core fidelity:** delegated to Hatari. Not a Talos risk.
- **Instrumentation + visualisation:** where Talos's real work and real risk now live.
- **Mega STE dual-speed (16/8 MHz) cycle-counting:** the one variant-specific timing complication to represent honestly (C-006).
- **Licensing:** Hatari is GPLv2; the fork and its patches inherit GPL. A non-GPL requirement would force the optional from-scratch core (Phase 5). Decide intent early (C-007).
- **All timing constants** must be sourced from authoritative references and bench-validated, never trusted from memory (C-008).

## Platform

Linux-first (Hatari's native home; development target is the ThinkPad P15 under Linux). Qt6 for the client. The Hatari fork builds with CMake. Windows and macOS are later considerations, inheriting from Hatari's own portability.
