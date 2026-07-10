# Phase 1 — Register-write capture over B1

> **Status:** Active
> **Provenance:** Claude (implementer), verified against the fork + EmuTOS 1.4 on 2026-07-10.
> **Last reviewed:** 2026-07-10
> **Why this status:** The B1 capture mechanism and the test-effect harness are proven end to end. The client UI (timeline + write markers) is the next build step.

---

How Talos captures hardware-register writes with their exact beam position, in
**pure B1** (no fork patch — D-005 satisfied), and the test effect that drives it.

## The capture mechanism (verified)

Hatari breakpoints support a **change condition**: when both sides of `!` are the
same expression, `BreakCond_CheckTracking` (breakcond.c:1141) freezes the right
side to the current value and sets a `track` flag, so the breakpoint fires when
the value *changes*. Catch a write to a register `A` like this:

1. **`break`** — stop first. **This is essential:** a breakpoint only arms on the
   stopped→running transition, so a bp set while already running never fires.
2. `bp ( $ffffXXXX ).w ! ( $ffffXXXX ).w :once` — one-shot change breakpoint on
   the (word) register. Use the **full `$ffffXXXX`** address and single `=`/`!`
   (not `==`).
3. `run`, then poll `status` until stopped (field 1 == `0`).
4. Read `regs` → `HBL` (scanline), `LineCycles` (cycle-in-line) give the beam
   position of the write; `mem ffffXXXX 2` gives the value written (plain hex
   address, no `$`, per protocol 0x1006).
5. Re-arm and repeat to walk successive writes.

Verified on the raster-bar effect: 20 writes to `$ffff8240` captured, each at a
distinct beam position (HBL 10, 20, 29, 39, … stepping ~10 lines; cycles spread
across the line) — exactly the write-to-cycle data the overlay/timeline needs.

### B1 vs B2

This is a *snapshot* capture: each write costs a break→run→read round-trip (tens
of ms). Fine for tens of writes on one effect (the Phase 1 target). A dense effect
(e.g. Spectrum 512, per-cycle palette) would overwhelm it — that is where the
**B2 register-write tap** (ARCHITECTURE §3: instrument the `ioMem` write path,
stream `(addr, value, cycle, line, cycle-in-line)`) becomes necessary, and where
the C-004/D-004 socket-bandwidth question gets measured. Not needed yet.

## The test effect (tests/effects/rasterbars.s)

A tiny ST program that writes the Shifter background-colour register `$ffff8240`
with an incrementing value and a delay, producing rolling colour bands and a
stream of writes spread across scanlines and cycles. Build with `vasm -Ftos`
(scripts/bootstrap-vasm.sh, scripts/build-effects.sh).

### Running an effect in Hatari — the quirks (all learned the hard way)

- **Clean config required.** A stray `~/.config/hatari/hatari.cfg` that sets a
  floppy in A: makes EmuTOS boot from the (missing) floppy instead of the GEMDOS
  drive, so the AUTO program never runs. Launch with `--configfile <empty>` to
  ignore user config.
- **GEMDOS AUTO folder, not `--auto`.** Put the program as an 8.3 `.PRG` in an
  `AUTO/` folder on a GEMDOS drive (`-d tests/effects/disk`). EmuTOS runs
  `\AUTO\*.PRG` at boot. (`--auto <path>` did not work here.)
- **EmuTOS runs AUTO programs in USER mode** (`sr=0300`), unlike Atari TOS. The
  program must `Super(0)` into supervisor before touching protected low RAM or
  hardware registers, or it bus-errors (`addr=$44e`, "Crash at text+…").
- **Boot delay.** EmuTOS shows a welcome screen; AUTO runs ~10–14 s in. Wait for
  it (or later: skip the welcome screen / fast-forward boot).

Launch recipe used for capture:

```bash
hatari --configfile empty.cfg --tos tos/etos512uk.img --machine st --sound off \
       -d tests/effects/disk        # boots, runs AUTO\RBARS.PRG
```

## Next: the client feature

- Capture loop in `session/`/a capture controller (break → arm → run → read).
- Store writes in the state model (F-215) as `(frameCycle, scanline, cycle, addr,
  value, pc)`.
- Draw write markers on the framebuffer at each beam position (BeamGeometry),
  distinct from the live beam crosshair; and a cycle-ordered timeline panel.
