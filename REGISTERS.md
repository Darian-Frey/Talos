# Talos — Registers

> **Status:** Active
> **Provenance:** Design session with Claude (primary architect/auditor).
> **Last reviewed:** 2026-07-08
> **Why this status:** Registers seeded from the `Atari_tools.md` scope and extended with items surfaced by the architecture work. Append-only from here.

---

Append-only. Feature IDs (F-NNN) continue the Talos numbering established in `Atari_tools.md` (F-201–) for a single consistent namespace across both documents. Constraint IDs (C-NNN) are Talos-project-scoped (C-001–) and cross-reference the ecosystem constraints in `Atari_tools.md` where relevant.

## Feature register (F-NNN)

- **F-201** Instrumentation layer over the cycle-exact core: expose beam position, register-write cycle, bus traffic, Blitter/DMA internal state. *(B1 where the existing protocol allows; B2 taps otherwise.)*
- **F-202** Live framebuffer + audio output driven by the instrumented core (framebuffer *taken* from Hatari, per D-007).
- **F-203** Beam-position overlay and register-write-to-cycle timeline for the current frame.
- **F-204** Interactive register-poking UI: palette (STE 4096, quirk-correct), hardware scroll, Blitter control, DMA sound, Microwire/LMC1992.
- **F-205** Machine selector (520/1040 ST, Mega ST, STE, Mega STE) with capability gating.
- **F-206** Region selector (PAL/NTSC) as a first-class timing setting.
- **F-207** Differential view: flip machine/region and show capabilities/timing appearing or collapsing.
- **F-208** Blitter memory-traffic visualisation. *(B2)*
- **F-209** DMA sound buffer-drain + LMC1992 EQ-curve visualisation. *(B2)*
- **F-210** Mega STE dual-speed (8/16 MHz) demonstration.
- **F-211** Effect prototyping workspace (raster bars, Spectrum 512, hardware scrollers).
- **F-212** Export effect as register sequence / asm stub; "verify on Hatari" round-trip.
- **F-213** Per-scanline framebuffer diff harness against stock Hatari (validation / regression).
- **F-214** Connection / session manager: launch or attach to the Hatari fork, negotiate protocol version and per-tap capabilities, degrade gracefully when a B2 tap is absent.
- **F-215** State model: per-frame ring of cycle/scanline-indexed events (register writes, beam positions, Blitter/DMA activity) that all panels read from.
- **F-216** Hatari configuration generation (machine/TOS/RAM/region), shareable in concept with Hermes' launch-config logic.
- **F-217** State-snapshot capture/restore (via Hatari's whole-system save/restore) for parking prototypes and seeding validation runs.
- **F-218** Secondary "reconstruct-from-registers" view (optional, teaching) — additional to, never replacing, F-202. *(per D-007)*

## Constraint / claim register (C-NNN)

- **C-001** Talos contains no emulation core of its own; correctness of emulation is delegated entirely to Hatari. Talos's own correctness scope is instrumentation, visualisation, and config.
- **C-002** B2 fork divergence from mainline Hatari must be kept minimal and rebasable; every B2 patch is justified only by a specific feature that B1 cannot serve (per D-005).
- **C-003** The candidate tap points (ARCHITECTURE §3) are indicative and **must be confirmed against the actual fork source in Phase 0** before B2 estimates are trusted.
- **C-004** Socket bandwidth for per-cycle event streams is unproven; it is the measured risk of Phase 1 and the trigger for the D-004 reversal (single-process embedding).
- **C-005** The Mega STE is dual-speed (16 MHz cache-resident, 8 MHz on ST-side bus access); raster cycle-counting is bimodal and both regimes must be represented, not averaged. *(= `Atari_tools.md` C-006.)*
- **C-006** Hatari is GPLv2; the fork, its patches, and a co-distributed Talos are treated as GPL. A non-GPL requirement forces Phase 5. *(= `Atari_tools.md` C-007; see D-010.)*
- **C-007** All timing constants (cycles-per-line, border windows, STE prefetch offsets, wakeup-state shifts) are sourced from authoritative references and bench-validated; where possible they are *read from Hatari* rather than hard-coded, to stay consistent with the instrumented core. *(= `Atari_tools.md` C-008.)*
- **C-008** The STE palette bit-order quirk (LSB of intensity stored as the top bit of each nibble) must be honoured in every palette decode path; it is a concrete correctness test, not an incidental detail. *(Relates to `Atari_tools.md` C-005.)*
- **C-009** A TOS image (or EmuTOS) is required at runtime and is supplied by the user; none ships with Talos (copyright).
- **C-010** The framebuffer is taken from Hatari, not reconstructed (per D-007); any reconstruction view is secondary and must never be mistaken for the authoritative output.
