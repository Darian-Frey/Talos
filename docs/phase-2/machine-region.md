# Phase 2 — Machine & region selection

> **Status:** Active
> **Provenance:** Claude (implementer), built and verified 2026-07-11.
> **Last reviewed:** 2026-07-11
> **Why this status:** Machine/region selectors, relaunch, region→geometry and a capability readout are in and verified. The richer differential view (palette 512↔4096 decode, panel gating for Blitter/DMA) grows as those panels arrive.

---

Phase 2 makes the four machines and two regions first-class (F-205/F-206/F-207).
Pure B1 — config generation only, no fork patches.

## Model (`client/src/model/Machine.h`)

`MachineType` (ST / Mega ST / STE / Mega STE) with a `MachineInfo` table giving the
Hatari `--machine` value and the capabilities that differ across the family:
Blitter, hardware scroll, DMA sound, palette colours (512 vs 4096), dual-speed.
Region maps to Hatari's `--country`: `us` = 60 Hz NTSC, `de` = 50 Hz PAL.

## Config / boot

- Machine → `--machine st|megast|ste|megaste`.
- Region → `--country` on a **multi-language EmuTOS** (`tos/etos1024k.img`), which
  sets the boot video frequency. Verified: `--country us` → `$ff820a=0xfc` (60 Hz),
  `--country de` → `0xfe` (50 Hz).
- `HatariLauncher::Config` gained `country`; `MainWindow` builds the machine/country
  from the selected `MachineType`/`VideoRegion` at (re)launch.

## UI

- Toolbar **Machine**, **Language** and **Region** combo boxes. Changing any of
  them updates the capability readout and, if a machine is running, **relaunches**
  Hatari with the new config and reconnects (clearing the previous machine's
  frame/state).
- **Language** maps to the EmuTOS `--country` together with region: English has
  both `uk` (PAL) and `us` (NTSC); other languages are PAL-only, so picking NTSC
  with a PAL-only language snaps the region back to PAL (kept honest by
  `reconcileRegion`). Default is English · PAL (`uk`).
- **Stop** terminates the running machine and disconnects (Launch/Stop enable-gate
  each other); an unexpected Hatari exit resets the same way.
- Region also drives `BeamGeometry` so the beam overlay uses the right PAL/NTSC
  constants (closes BUG-002).
- A **Machine** dock lists the current machine's capabilities (✓/✗ + palette
  colour count) — the seed of the differential view.

Verified: ST/PAL, STE/NTSC and Mega STE/PAL all launch, boot and capture a frame.

## Palette panel (the differential showpiece) — done

`view/PaletteView` + `model/Palette` read the 16 colour registers (`mem ff8240 20`)
and decode them exactly as Hatari does (`Screen_SetupRGBTable`): each 4-bit gun
nibble reordered per the C-008 quirk (bit 3 = LSB) then scaled 4→8 bit. The dock
shows the 16 live swatches (with `$rgb` values), a header (`512`/`4096` colours,
`3`/`4` bits/gun) and a per-gun intensity ramp at the machine's resolution.

Verified against real boots: ST default `[0]=$777` → white 238; STE `[0]=$fff` →
white 255 (bit 3 used). Switching ST↔STE shows the palette gain the 4th bit and
the ramp go 8→16 levels — the 512↔4096 differential, legible at a glance.

## Next (this phase)

- **Capability gating** — grey out controls/panels for absent hardware as the
  Blitter/DMA/scroll panels are built (mostly Phase 3 surface).
- Read the region back from the core (`$ff820a`) rather than only setting it.
