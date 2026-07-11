# Talos — Known issues & limitations

> **Status:** Active
> **Provenance:** Claude (implementer), opened 2026-07-10 during Phase 1.
> **Last reviewed:** 2026-07-11
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
Follow-up: still *set* rather than *read from the core* — verify against `$ff820a`
when convenient.

**BUG-003 — Beam geometry assumes ST low resolution.**
Status: Open · Severity: Medium · Area: view/BeamGeometry
The `(scanline, cycle) → pixel` mapping is derived for low-res (320×200, 2× zoom,
832×552 surface). Medium/high res change the doubling and byte layout; the overlay
would mis-register. Should read the resolution and pick constants accordingly.

**BUG-004 — Framebuffer is screenshot-poll, not a live stream.**
Status: Open · Severity: Low · Area: app, protocol
Frames are grabbed via `console screenshot` to a file each refresh (~4 Hz), not
streamed. Fine for M0/Phase 1. If live video or per-cycle event volume is needed,
this is the C-004/D-004 measurement point (shared-memory embedding fallback).

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
Status: Open · Severity: Low · Area: session, tests/effects
EmuTOS shows a welcome screen and only runs `AUTO` programs ~10–14 s into boot.
The launcher waits blindly. Could skip the welcome screen or fast-forward boot,
and detect "effect running" (PC in the program) rather than sleeping.

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
