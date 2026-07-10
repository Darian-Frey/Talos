# Test effects

> **Status:** Active
> **Provenance:** Claude (implementer), Phase 1.
> **Last reviewed:** 2026-07-10
> **Why this status:** First effect (rasterbars) built and verified driving the B1 register-write capture. More (border removal, etc.) come as harder harness cases.

---

Small ST programs that write hardware registers per frame, to drive Talos's
register-write-to-cycle capture (and, later, the validation harness). Assembled
with vasm (`scripts/bootstrap-vasm.sh`, then `scripts/build-effects.sh`).

- `rasterbars.s` — writes the background-colour register `$ffff8240` across the
  frame (rolling colour bands + writes spread over the beam). Built to
  `disk/AUTO/RBARS.PRG`.

## Running

Effects run from the `AUTO` folder of the GEMDOS drive directory `disk/`, which
Hatari mounts with `-d tests/effects/disk`. Use a clean Hatari config so a stray
user floppy setting doesn't hijack the boot drive. See
`docs/phase-1/register-writes.md` for the full recipe and the EmuTOS quirks
(AUTO = user mode → the program calls `Super(0)`; ~10–14 s boot delay).

`disk/AUTO/RBARS.PRG` is committed as a ready-to-run fixture (106 bytes) so the
effect runs without building vasm.
