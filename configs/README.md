# Talos — Hatari configuration templates (F-216)

> **Status:** Active
> **Provenance:** Claude (implementer), Phase 0 scaffold.
> **Last reviewed:** 2026-07-09
> **Why this status:** Directory reserved. Config generation is exercised from Phase 0 (launch) and becomes first-class in Phase 2 (machine/region selection).

---

Templates and generated `hatari.cfg` fragments for driving the fork: machine type
(520/1040 ST, Mega ST, STE, Mega STE), TOS image, RAM, and region (PAL/NTSC).

Talos sets these via Hatari's config file, command-line options, and run-time
`setopt`/control-socket surface (ARCHITECTURE §7). The generation logic is a
candidate shared component with Hermes' launch-config — noted, not yet decided
(open thread in CLAUDE.md; revisit when Hermes work begins).

ROM paths point into `tos/` (gitignored, user-supplied — C-009).
