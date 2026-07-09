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

## Present in this working tree (not committed — gitignored)

- `etos512uk.img` — EmuTOS 1.4, 512 KB UK build, 524288 bytes,
  sha256 `f3177763bd3f2a984bf7d2f112f4a3bb4a6d20c7e2d77549f6973bb884edb49e`.
  Fetched from SourceForge `emutos/1.4/emutos-512k-1.4.zip` on 2026-07-09.
  This is the dev-default ROM the Phase 0 pipe test booted. Re-fetch with the
  same file if starting from a fresh clone.
