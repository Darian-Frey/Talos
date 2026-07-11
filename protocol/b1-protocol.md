# B1 — the existing remote-debug protocol (verified against fork source)

> **Status:** Active
> **Provenance:** Claude (implementer), reading `src/debug/remotedebug.c` + `src/debug/vars.c` in tattlemuss/hatari @ 9832e006, 2026-07-09.
> **Last reviewed:** 2026-07-11
> **Why this status:** Command set and beam-data path verified from source. Exact response payloads (per-command field order) still need a line-by-line read of each handler before the client parser is written.

---

This is the protocol Talos speaks with **no fork changes** (B1, D-005). Source of
truth: `src/debug/remotedebug.c`.

## Transport & framing

- TCP socket (hrdb connects a debugger client to Hatari).
- **Protocol ID `0x1007`** (`REMOTEDEBUG_PROTOCOL_ID`) — negotiated so client and
  Hatari can detect a version mismatch. History: 0x1003 reset cmds, 0x1004 ffwd,
  0x1005 memfind + stramsize, 0x1006 hex-only addr/size, 0x1007 savebin.
  The session manager (F-214) must check this and degrade/refuse on mismatch.
- **Framing (verified on the wire 2026-07-09):** tokens are separated by
  `SEPARATOR_VAL = 0x1`; every message ends with a `0x0` terminator (both
  directions — commands sent to Hatari are also `0x0`-terminated).
  Command replies begin `OK` (or `NG` + hex error code) then `0x1`-separated
  tokens then `0x0`.
- **Connect handshake (verified):** on `accept()` Hatari immediately pushes,
  unprompted:
  - `!connected` `0x1` `1007` — the protocol id (`0x1007`); check for mismatch.
  - `!config` `0x1` `<machineType>` `0x1` `<?>` `0x1` `<stramsize>` — e.g.
    `!config|0|0|100000` = ST, 1 MB.
  - `!status` `0x1` `<running>` `0x1` `<PC>` `0x1` `<ffwd>` — e.g.
    `!status|1|E0EE10|0`.
  Async **notifications are prefixed `!`** (`!connected`, `!config`, `!status`)
  and are pushed on state changes (break hit, config/reset, ffwd) as well as at
  connect — distinct from `OK`/`NG` command replies. F-214 must handle both an
  unsolicited `!`-stream and request/reply interleaved on the same socket.

## Command set (the 21 B1 verbs)

From `remoteDebugCommandList[]`. These cover essentially all of Talos Phase 0–2.

| Command | Purpose (Talos use) |
|---|---|
| `status` | running-flag + PC — cheap poll |
| `break` | stop the running emulation |
| `run` | resume |
| `step` | single-step one instruction |
| `regs` | **CPU registers + Hatari variables** — incl. beam counters, see below |
| `mem` / `memset` | read / write memory (hardware registers live in memory map) |
| `bp` / `bplist` / `bpdel` | breakpoints — set / list / delete |
| `symlist` | symbol list |
| `exmask` | exception mask |
| `console` | pass a line to the debugui console (fallback for anything unmapped) |
| `setstd` | redirect console std output |
| `infoym` | YM2149 (sound chip) info |
| `profile` | cycle/instruction profiling |
| `resetwarm` / `resetcold` | machine reset |
| `ffwd` | fast-forward |
| `memfind` | search memory |
| `savebin` | dump memory region to file |

There is **no `run-to-HBL` / `run-to-VBL` verb** in this list — hrdb's HBL/VBL
stepping is built client-side on top of `bp` (breakpoint on the counters) + `run`.
Talos does the same; not a B2 gap.

## Beam / cycle position over B1 — the important result

`regs` returns CPU registers **plus a subset of Hatari debug "variables"**
(`src/debug/vars.c`). Those variables include the video beam position:

| Variable | Meaning | Source |
|---|---|---|
| `LineCycles` | **cycles since HBL = cycle-in-line (X beam)**, divisible by 4 | `Video_GetPosition()` |
| `FrameCycles` | cycles since VBL (frame position) | `Video_GetPosition()` |
| `HBL` | HBL interrupt count ≈ **scanline (Y beam)** | `nHBL` |
| `VBL` | VBL interrupt count (frame number) | `nVBLs` |
| `CycleCounter` | absolute cycle counter | — |

**Consequence for Phase 1 (F-203):** the beam-position overlay and
register-write-to-cycle mapping can be built in **pure B1** — stop/step, read
`LineCycles` + `HBL`, and you have (X, Y) of the beam at that instant. Mapping a
register write to its cycle = break on the write (`bp` on the address), read the
counters. **No fork patch to begin.**

**Where B2 is still required:** a *continuous stream of every register write
tagged with its cycle while running at full speed*. B1 only surfaces the counters
when the emulation is stopped, so the live timeline of a whole frame at speed
needs the register-write tap (B2, ARCHITECTURE §3) + a new packet. Escalate then,
per D-005 — and measure the socket bandwidth there (C-004, D-004 reversal test).

## B2 extensions (fork patches) — added by Talos

These commands do **not** exist in stock Hatari; they are added by the Talos
patch series under `patches/` (see that dir's README). Documented here so the
one wire format lives in one place.

### `blittrace` — blitter memory-traffic trace (F-208, `patches/0001-*`)

Backs the Blitter traffic view. Opt-in and off by default, so it never perturbs
emulation (the D-009 diff harness stays valid). Framing is the standard one
(`0x1`-separated tokens, `0x0`-terminated; reply starts `OK`/`NG`).

| Form | Effect | Reply |
|---|---|---|
| `blittrace on` | enable + clear the trace buffer | `OK` |
| `blittrace off` | disable | `OK` |
| `blittrace clear` | clear without disabling | `OK` |
| `blittrace` | dump accumulated entries | see below |
| `blittrace <other>` | unknown sub-command | `NG 1` |

Dump reply: `OK` `0x1` `<count:hex>` then, per entry, five `0x1`-separated hex
tokens — `addr` `cycle_hi` `cycle_lo` `value` `flags`. `flags` bit0 = write
(else read); bit1 = **blit-complete marker** (closes an operation — a non-hog
blit spans several bus bursts but yields one marker at the true end-of-blit).
The buffer caps at 16384 entries. Taps: `blitter.c` `Blitter_ReadWord` /
`Blitter_WriteWord` (each memory access) and the `y_count==0` branch of
`Blitter_Start` (the marker); `cycle` is `CyclesGlobalClockCounter`.

## Function seam noted for the B2 video tap

`Video_GetPosition(&frameCycles, &lineNumber?, &lineCycles)` in `src/video.c` is
the canonical beam-position accessor. The B2 beam/border tap (ARCHITECTURE §3)
should hang off the same internal state this reads, so B1 and B2 report identical
positions (consistency, C-007).

## Empirically verified end to end (2026-07-09)

Booted the fork with EmuTOS 1.4 (`tos/etos512uk.img`), connected a raw TCP client
to `127.0.0.1:56001`, and exercised the pipe:

- Connect greeting arrived exactly as above (`!connected|1007`, `!config|0|0|100000`,
  `!status|1|E0EE10|0`), PC in the `$E0xxxx` EmuTOS ROM range.
- `regs\0` returned **49 key/value pairs** — CPU registers plus the video
  variables. Beam state read live: `HBL=198` (scanline), `LineCycles=46`
  (cycle-in-line), `FrameCycles=101422`, `VBL=293`, `CycleCounter=46950710`.

This confirms, on the wire and not just in source, that the beam-position overlay
and register-write-to-cycle mapping are achievable in pure B1 (D-005).

## Still to read before writing the client parser

- Exact token layout of `mem` / `memset` / `bp` responses (not yet exercised).
- Arg encoding for commands that take arguments (how `RemoteDebug_Parse` splits
  multi-arg commands — `regs` is arg-free; `mem <addr> <count>` is the next to try).
- Confirm the middle field of `!config` (currently `0` for ST + 1 MB) and the
  trailing `!status` field (ffwd flag).
