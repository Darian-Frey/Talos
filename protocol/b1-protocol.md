# B1 — the existing remote-debug protocol (verified against fork source)

> **Status:** Active
> **Provenance:** Claude (implementer), reading `src/debug/remotedebug.c` + `src/debug/vars.c` in tattlemuss/hatari @ 9832e006, 2026-07-09.
> **Last reviewed:** 2026-07-09
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
- Token terminator `SEPARATOR_VAL = 0x1` (below ASCII 32 so 32–255 are data).
  Responses are `OK` / `NG` then separator-delimited tokens then a terminator.

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

## Function seam noted for the B2 video tap

`Video_GetPosition(&frameCycles, &lineNumber?, &lineCycles)` in `src/video.c` is
the canonical beam-position accessor. The B2 beam/border tap (ARCHITECTURE §3)
should hang off the same internal state this reads, so B1 and B2 report identical
positions (consistency, C-007).

## Still to read before writing the client parser

- Exact token layout of each response (`regs`, `mem`, `status`, `$config`
  notification) — field order and encoding, handler by handler.
- The asynchronous **notifications** (`NotifyConfig` / `NotifyStatus`): when
  Hatari pushes state unprompted (break hit, config, ffwd status) vs. request/reply.
