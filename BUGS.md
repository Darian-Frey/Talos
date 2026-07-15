# Talos — Known issues & limitations

> **Status:** Active
> **Provenance:** Claude (implementer), opened 2026-07-10 during Phase 1.
> **Last reviewed:** 2026-07-15
> **Why this status:** Live register of open issues and by-design limitations. Fixed bugs are not tracked here — they live in git history.

---

Append-only. IDs are `BUG-NNN` (never renumber; supersede, don't delete). When an
item is resolved, change its **Status** to `Fixed` with the commit/short reason
rather than removing it. Scope: defects and limitations in **Talos** (the client,
its B2 patches, its config) — not upstream Hatari behaviour or local environment
quirks (those go in the relevant phase doc).

Fields: **Status** (Open / Fixed / Won't-fix / Superseded), **Severity**
(Low / Medium / High), **Area**, one-line summary, then detail.

---

**BUG-001 — Launcher does not force a clean Hatari config.**
Status: Fixed · Severity: Medium · Area: session/HatariLauncher
A stray user `~/.config/hatari/hatari.cfg` (e.g. a floppy set in A:) can hijack
the boot drive so a GEMDOS-`AUTO` effect never runs. The launcher should pass
`--configfile <controlled>` so runs are reproducible regardless of user config.
(See `docs/phase-1/register-writes.md`.)
Fixed: `HatariLauncher::Config::cleanConfig` (default on) launches with an empty
`--configfile`.

**BUG-002 — Region is hard-coded to PAL 50 Hz.**
Status: Fixed · Severity: Medium · Area: view/BeamGeometry, app
The beam overlay assumes PAL. NTSC constants exist in `BeamGeometry` but nothing
selects them, and region is not read from Hatari. Resolve in Phase 2 (machine/
region selection); prefer reading the region from the core over assuming it.
Fixed: Phase 2 adds a region selector (PAL/NTSC) that drives both `BeamGeometry`
and the Hatari boot frequency (via `--country` on the multi-language EmuTOS).
Follow-up (done 2026-07-14): the region is now also *read from the core* —
`refresh()` reads the sync-mode register `$ff820a` (bit 1 = 50/60 Hz;
bench-validated uk→`0xfe`→PAL, us→`0xfc`→NTSC) and the overlay geometry + region
combo follow it, so the UI reflects what actually booted rather than the guess.

**BUG-003 — Beam geometry assumes ST low resolution.**
Status: Fixed · Severity: Medium · Area: view/BeamGeometry
The `(scanline, cycle) → pixel` mapping is derived for low-res (320×200, 2× zoom,
832×552 surface). Medium/high res change the doubling and byte layout; the overlay
would mis-register. Should read the resolution and pick constants accordingly.
Fixed: bench-validated that **medium res shares the low-res geometry** (med renders
640 px / zoom 1 where low renders 320 / zoom 2 → same 832×552 surface and mapping),
so only **high-res mono** needed distinct constants (640×400, no borders, 224 cyc/
line, 4 px/cycle, from `video.h` *_71HZ). `BeamGeometry` selects mono when the taken
frame is ≤450 px tall — reading the resolution from the rendered surface rather than
assuming. `talos --mono` reaches it. See `docs/phase-1/beam-geometry.md`.

**BUG-004 — Framebuffer is screenshot-poll, not a live stream.**
Status: Fixed · Severity: Low · Area: app, protocol
Frames are grabbed via `console screenshot` to a file each refresh (~4 Hz), not
streamed. Fine for M0/Phase 1. If live video or per-cycle event volume is needed,
this is the C-004/D-004 measurement point (shared-memory embedding fallback).
Fixed (measured 2026-07-14): the screenshot round-trip (command → PNG written →
loaded) is ~17 ms median (~60 fps capable), so the ~4 Hz was an *artificial timer
limit*, not a path/bandwidth constraint — the C-004 answer is that the file-poll
is adequate and the D-004 shared-memory reversal is **not** needed for frames.
Raised the live refresh to ~20 Hz (`kLiveIntervalMs` 250→50) and decoupled the
rarely-changing palette/region reads to ~4 Hz so the per-frame path stays light.
(Still a poll, not a push stream — but proven sufficient; a B2 framebuffer packet
remains an option only if per-cycle event volume ever demands it.)

**BUG-005 — B1 register-write capture is a slow snapshot.**
Status: Open · Severity: Low · Area: capture (planned)
Each captured write costs a break→run→read round-trip (tens of ms). Fine for tens
of writes on one effect; a dense effect (e.g. Spectrum 512) needs the B2 `ioMem`
write tap (ARCHITECTURE §3). Escalate per-feature when a case demands it (D-005).

**BUG-006 — GUI is only exercised headless in automation.**
Status: Open · Severity: Low · Area: client, CI
`--selftest` drives the real MainWindow slots under `QT_QPA_PLATFORM=offscreen`,
but nothing renders on a physical display in automation. Interactive checks are
manual. Acceptable for now; revisit if a display-level regression slips through.

**BUG-007 — Effect boot is slow (~10–14 s) with no skip.**
Status: Fixed · Severity: Low · Area: session, tests/effects
EmuTOS shows a welcome screen and only runs `AUTO` programs ~10–14 s into boot.
The launcher waits blindly. Could skip the welcome screen or fast-forward boot,
and detect "effect running" (PC in the program) rather than sleeping.
Fixed (2026-07-14): effect launches now boot with `--fast-forward on`, and the
client turns it off (`ffwd 0`) once it detects the effect running — PC stable in
a small RAM window (the TPA loop) for several polls, which boot code (running from
ROM $E00000+) never does; a ~6 s safety timeout falls back to normal speed if
detection misses. Measured: the effect is reached in ~0.6 s wall vs ~5.8 s
(fast-forward ~2000 VBL/s vs ~50), and detection returns it to normal speed.

**BUG-008 — Mega STE dual-speed is a flat toggle; per-access bimodality is not visualisable.**
Status: Won't-fix (by design) · Severity: Low · Area: view (planned F-210), docs
Hatari models Mega STE speed as a single global 8/16 MHz toggle: bit 1 of `$FF8E21`
scales the per-raster-line cycle budget (`<< nCpuFreqShift`), while bit 0 (the
cache) is ignored — *"we handle only bit 1, bit 0 is ignored (cache is not
emulated)"* (`external/hatari/src/ioMemTabSTE.c:35`; every 8↔16 switch funnels
through `Configuration_ChangeCpuFreq`, `configuration.c:1260`). The real machine's
per-access behaviour (16 MHz on cache hits, dropping to 8 MHz on ST-bus accesses)
is therefore absent from the core. F-210 visualises the two speed *settings* and
their raster-budget effect — an effect that holds at one and breaks at the other —
but cannot honestly show intra-setting cache/bus bimodality: synthesising it would
mean adding emulation Hatari lacks, forbidden by D-002. Not a defect but a bound on
what the visualiser can truthfully show; records the C-005 clarification.

---

**BUG-009 — Live view tears animated effects (mid-render screenshot grab).**
Status: Fixed · Severity: Medium · Area: app (live refresh)
The live view polled `console screenshot` while Hatari free-ran at real time, so it
grabbed a mid-render surface — the top of the frame from the new frame, the bottom
from the previous. Static effects never showed it (every frame is identical), but
the STE hardware scroller (the first *animated* effect, F-211/F-212) sheared
intermittently, worse at higher scroll speeds (bigger per-frame offset). The effect
itself is correct: stopped at a frame boundary the memory is stable and renders
clean (the round-trip harness and the exported `.PRG` are seam-free) — this was a
capture-timing artifact only. Sourced against the fork: Hatari yields a complete
frame only when stopped at a VBL *under fast-forward*; at real time (even VBL-
stopped) the render is throttled and grabs tear. Fixed (2026-07-15): the live tick
now grabs coherently — break, `ffwd 1`, run to the next VBL (a complete frame),
screenshot, `ffwd 0`, resume — with an in-flight guard so ticks can't overlap and
Break disabling live so a paused machine is never silently resumed. Boot keeps the
plain grab (static screen; preserves the BUG-007 detection poll). Costs a little
preview rate (~20→~14 Hz) and fixes tearing for all animated content. Verified: a
speed-8 scroller frame grabbed by the real client is clean and legible.
