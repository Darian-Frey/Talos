# Phase 1 — Beam ↔ framebuffer geometry

> **Status:** Active
> **Provenance:** Claude (implementer), sourced from the Hatari fork (tattlemuss/hatari @ 9832e006) and verified 2026-07-10; multi-resolution (BUG-003) added and bench-validated 2026-07-14.
> **Why this status:** The PAL/NTSC colour mapping (low + medium res) and the high-res mono mapping are confirmed against source and bench-validated. STE fine-scroll and border-removal width changes are noted but not yet implemented.

---

How Hatari's **taken framebuffer** maps to the ST video (scanline, cycle) space,
so the beam overlay (F-203) registers exactly to the picture. Every constant is
from Hatari source (C-007), not guessed. Implemented in
`client/src/view/BeamGeometry.{h,cpp}`.

## Surface dimensions (ST low-res, full overscan, status bar OFF)

`Screen_SetSTResolution()` (screen.c:558-651) builds a **832 × 552** surface:

- low-res 320×200 doubled to 640×400, `nScreenZoomX = nScreenZoomY = 2` (screen.c:569-597);
- borders (screen.c:225-249): left = right = 48 ST px (clamped), top = `OVERSCAN_TOP` 29, bottom = `MAX_OVERSCAN_BOTTOM` 47;
- width = (48 + 320 + 48) × 2 = **832**; height = (29 + 200 + 47) × 2 = **552**.

The status bar (if on) adds height below this; we launch with `--statusbar off`.

## The mapping

`nHBL` ("HBL") is the **absolute scanline from 0 at the top of the frame**
(vars.c:191; video.c). `LineCycles` is the cycle within the line, **0..nCyclesPerLine**,
quantised to multiples of 4 (`Video_GetPosition` → `Video_ConvertPosition`,
video.c:1142-1190; vars.c:40-49).

**PAL 50 Hz** (nCyclesPerLine 512, 313 lines):

```
x = (LineCycles − 8)  × 2      // 8 = LINE_START_CYCLE_50 (56) − left border (48)
y = (nHBL       − 34) × 2      // 34 = FIRST_VISIBLE_HBL_50HZ
```

**NTSC 60 Hz** (nCyclesPerLine 508, 263 lines):

```
x = (LineCycles − 4) × 2       // 4 = LINE_START_CYCLE_60 (52) − 48
y = (nHBL       − 5) × 2       // 5 = FIRST_VISIBLE_HBL_60HZ (34 − 29)
```

Both put the display top-left at output (96, 58).

### Visible ranges (PAL) — outside these the beam is in blanking, not drawn

| Axis | Rendered | Display only |
|---|---|---|
| x | LineCycles [8, 424) → [0, 832) | display cycles 56..376 → x 96..736 |
| y | nHBL [34, 310) → [0, 552) | display lines 63..263 → y 58..458 |

`BeamGeometry::toPixel()` returns `nullopt` outside the rendered rectangle so the
overlay never lies about a beam position that isn't on screen.

### Reference points (unit-tested, `ALL PASS`)

- PAL (34,8)→(0,0); (63,56)→(96,58); (63,375)→(734,58); (309,56)→(96,550).
- off-frame: hblank (258,428); vblank (320,56); negative (63,4).
- NTSC (5,4)→(0,0); (34,52)→(96,58).

## Notes / limits

- **Surface size is fixed at 832×552 regardless of border removal** — border tricks
  fill *more of the existing buffer* (they don't resize `sdlscrn`). Ideal for
  Phase 1: a left-border-off write makes pixels appear in the normally-black left
  border, and the beam overlay already covers that region.
- **X precision is 4 cycles (8 px)** — `LineCycles` is quantised to multiples of 4.
- **All three ST resolutions supported** (BUG-003, bench-validated 2026-07-14):
  - **Low-res** — 832×552, `x=(cyc−8)·2`, `y=(hbl−34)·2` (above).
  - **Medium-res** — the *same* geometry. `Screen_SetSTResolution` renders med at
    640 display px / zoom 1 exactly where low renders 320 / zoom 2, so the surface
    is still 832×552 and the (cycle,scanline)→pixel mapping is identical. Confirmed
    by screenshot: a med-res effect (`$ff8260&3==1`) yields an 832×552 frame.
  - **High-res mono** — a distinct surface (`$ff8260&3==2`): **640×400**, no
    overscan borders, 71 Hz. Constants from `video.h`: `CYCLES_PER_LINE_71HZ 224`,
    `SCANLINES_PER_FRAME_71HZ 501`, `FIRST_VISIBLE_HBL_71HZ 34`,
    `LINE_START_CYCLE_71 0`, display 0..160 cycles → 640 px = **4 px/cycle**, no
    line doubling:

    ```
    x = (LineCycles − 0) × 4      // 640 display px / 160 display cycles
    y = (nHBL       − 34) × 1      // no doubling; 400 visible lines from HBL 34
    ```

  `BeamGeometry` selects mono when the taken frame's height ≤ 450 (mono 400 vs
  colour 552) — i.e. it reads the resolution *from the rendered surface* rather
  than assuming. Reach mono with `talos --mono` (`--monitor mono`).
- **STE fine scroll / prefetch** shift some windows (Phase 2/3); PAL/NTSC ST here.
- Region is currently fixed to PAL (the launcher's config). Phase 2 makes it
  selectable and reads it from Hatari rather than assuming.

## Border-removal hooks (for the next Phase 1 step)

Per-line border state: `ShifterFrame.ShifterLines[hbl].BorderMask` with flags at
video.c:473-483 (`BORDERMASK_LEFT_OFF`, `BORDERMASK_RIGHT_OFF`, …). Render-time
interpretation around video.c:1233-1405. Vertical (bottom/top border open):
`nEndHBL`/`nLastVisibleHbl`, video.c:509-510, 4482-4483. These are the seams the
register-write-to-cycle timeline and the border-open visualisation will read.
