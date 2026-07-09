# TOS / EmuTOS ROMs — user-supplied

> **Status:** Active
> **Provenance:** Claude (implementer), Phase 0 scaffold.
> **Last reviewed:** 2026-07-09
> **Why this status:** Policy fixed by C-009 / D-010 — no ROM ships with Talos. This directory holds ROMs the user drops in locally; its contents are gitignored.

---

**Nothing in here is committed** (see `.gitignore`) — TOS images are copyright and
Talos ships none (C-009).

Place ROM images here and point `configs/` at them:

| Machine | Recommended ROM |
|---|---|
| Development / CI default (all machines) | **EmuTOS** (free, GPL-compatible, redistributable) |
| 520 / 1040 ST, Mega ST | TOS 1.04 |
| STE | TOS 1.62 |
| Mega STE | TOS 2.06 |

For the per-scanline diff harness (D-009), only requirement is that Talos-driven
and stock Hatari use **the same** ROM — so EmuTOS is sufficient for validation.
Real ROMs matter for authentic TOS-era behaviour and the Phase 2 differential view.

EmuTOS: https://emutos.sourceforge.io/ (may be committed if we later choose to
bundle it, since it is freely redistributable — decision deferred).
