# Talos — User Manual

> **Status:** Active
> **Provenance:** Claude (implementer), written 2026-07-16 from the client source (`client/`) and harnesses (`harness/`); extended for the Phase 6 tabs 2026-07-19.
> **Last reviewed:** 2026-07-19
> **Why this status:** Practical how-to for the client through Phase 6 (M0–M4 + F-217, plus the Phase 6 tabs §7c–§7i and the corpus runner §8). For the design rationale read `CLAUDE.md`, `ARCHITECTURE.md` and `DECISIONS.md`; for the authoritative feature/bug state read `REGISTERS.md`, `BUGS.md` and `git log`.

---

## 1. What Talos is

Talos is a Qt6 **visualiser** that drives a patched **Hatari** (the Atari ST/STE
emulator) over a remote socket to make ST/STE hardware timing **visible** — where
on the scanline each register write lands, and why an effect works — across all
four machines (520/1040 ST, Mega ST, STE, Mega STE) and both video regions
(PAL/NTSC).

Talos **emulates nothing**. Hatari does the emulation; Talos instruments and
draws it. It also lets you *author* small effects (raster bars, vertical bands, an
STE hardware scroller), run them, and export a runnable `.PRG` + a portable
register sequence that reproduces in stock Hatari.

Two processes talk over TCP port **56001** (protocol `0x1007`): the Talos client
and the Hatari fork. Talos launches Hatari for you (or attaches to a running one).

---

## 2. First-time setup

Talos needs: a C++/Qt6 toolchain, the Hatari fork, the `vasm` assembler (for
effect authoring), Python 3 with `numpy` + `Pillow` (for the verify harnesses),
and a **TOS/EmuTOS ROM image you supply yourself** (none ships with Talos — it is
copyright, C-009).

```bash
# 0. Check the toolchain (read-only report; non-zero exit if something is missing)
scripts/check-deps.sh

# 1. Clone + build the Hatari fork (lands in external/hatari/, gitignored)
scripts/bootstrap-hatari.sh

# 2. Build the vasm assembler (needed for the Raster/Scroller "Build" & "Export")
scripts/bootstrap-vasm.sh

# 3. Provide a TOS image. A multi-language EmuTOS is best (lets the region/
#    language selectors work). Put it where Talos looks by default:
#      tos/etos1024k.img          (or point --tos / $TALOS_TOS at your own)

# 4. Build the Talos client (CMake presets: debug | release)
cmake --preset debug
cmake --build build/debug
# -> the binary lands at build/debug/bin/talos
```

The Hatari fork is **never vendored** into this repo (GPLv2 upstream, separate
process; D-004/D-005). Talos tracks only its own B2 patches under `patches/`.

---

## 3. Launching Talos

```bash
build/debug/bin/talos [options]
```

With no options it launches Hatari on a **520/1040 ST, PAL, English**, using the
default Hatari/TOS paths, and connects.

| Option | Meaning | Default |
|---|---|---|
| `--hatari <path>` | Patched Hatari binary | `external/hatari/build/src/hatari` (or `$TALOS_HATARI`) |
| `--tos <path>` | TOS/EmuTOS ROM image | `tos/etos1024k.img` (or `$TALOS_TOS`) |
| `--machine <type>` | `st` \| `megast` \| `ste` \| `megaste` | `st` |
| `--region <r>` | `pal` \| `ntsc` | `pal` |
| `--language <lang>` | `english`\|`german`\|`french`\|`spanish`\|`italian`\|`swedish` | `english` |
| `--mono` | Monochrome monitor (high-res 640×400) | off |
| `--no-fast-boot` | Boot effects at real speed (don't fast-forward the boot) | fast boot on |
| `--headless` | Run Hatari off-screen (no Hatari window) | off |
| `--attach` | Attach to an already-running Hatari instead of launching | off |
| `--host <host>` | Remote host to connect to | `127.0.0.1` |
| `--effect <dir>` | GEMDOS drive dir whose `AUTO/` holds an effect to auto-run | — |
| `--selftest <png>` | Headless CI: auto-start, save one frame, exit | — |
| `--selftest-capture <png>` | Headless CI: run `--effect`, capture writes to `--capture-reg`, save + print, exit | — |
| `--capture-reg <hex>` | Register for `--selftest-capture` | `ffff8240` |

The machine/region/language you pass seed the on-screen selectors; once the window
is up, those combos take over.

Example — launch an STE in PAL and auto-run an effect from a GEMDOS folder:

```bash
build/debug/bin/talos --machine ste --region pal --effect tests/effects/disk
```

---

## 3a. Loading a demo or program

Talos isn't only for effects you build in it — you can point its instrumentation
(beam overlay, register-write capture, disassembly, MFP, cycle counters…) at any
ST program or demo. Toolbar **Open…**, then pick a file:

- **`.PRG` / `.TOS`** — staged on a GEMDOS drive (mounted `C:`) under `AUTO\` and
  **auto-run** on boot, so the program is up and instrumentable immediately.
  (`.TTP` / `.APP` land on `C:` to run from the ST desktop by hand.)
- **`.ST` / `.MSA` / `.STX` / `.DIM` / `.IPF` / `.ZIP`** — mounted as a floppy in
  **drive A** and **booted**: a bootable demo disk runs its own bootsector; a
  data disk brings up the desktop with `A:` available.

The load **relaunches** on the current **machine / region** (set those first — a
demo may need a specific model), boots in **real time** (not fast-boot) so you can
watch it, and replaces any built effect, loaded program or snapshot. Then use
**Break / Run→Line / Capture / Trace** as usual to see what it's doing. Single-file
programs work directly; multi-file programs are best run from their disk image.
Sound is off (Talos runs Hatari muted for reproducible instrumentation).

**Multi-disk demos and drive B — the Disks… dialog.** Many demos span several
disks and ask you to "insert disk 2". Toolbar **Disks…** manages both drives:

- **Insert… / Eject** on **drive A** or **drive B**. On a *running* machine this is
  a live **hot-swap** — the floppy changes under the demo (via a small patched
  `floppy` command that raises the drive's media-change), so it reads the new disk
  without a reboot. When nothing is running, it just sets the drives for the next
  launch.
- **Boot to clean desktop** — eject everything (disks, program, snapshot) and
  relaunch to a bare TOS desktop.

So the flow for a multi-disk demo is: **Open…** the boot disk, let it run, and when
it asks for the next disk, **Disks… → drive A → Insert…** the next image and
continue — or pre-load disk 2 in **drive B** if the demo looks there.

---

## 4. The interface

### The toolbar (top, left → right)

**Session / machine**
- **Machine** — 520/1040 ST · Mega ST · STE · Mega STE. Changing it relaunches
  Hatari on that machine.
- **RAM** — ST memory size: 512 KB · 1 MB · 2 MB · 4 MB (`--memsize`). 512 KB is
  an authentic 520 ST; **1 MB** (the default) runs most demos; pick more for the
  memory-hungry ones. Changing it relaunches (RAM size is fixed at boot).
- **Language** — EmuTOS language (also sets the country/region EmuTOS boots in).
- **Region** — video region, 50 Hz PAL / 60 Hz NTSC. Talos also *reads back* the
  real region from `$ff820a`, so the overlay geometry follows reality even if an
  effect flips sync mode.
- **Clock** — CPU clock 8/16 MHz. Only meaningful on the dual-speed **Mega STE**
  (F-210); disabled otherwise.

  *Machine, RAM, Region and Language are **remembered across sessions** — Talos
  starts up with your last selection. An explicit `--machine` / `--region` /
  `--language` on the command line overrides the remembered value for that run.*
- **Launch** (or **Connect** with `--attach`) — start Hatari + connect.
- **Stop** — stop the running machine and disconnect.

**State (F-217)**
- **Save state** — snapshot the whole machine to a file (park a prototype, or seed
  a validation run). Works while running or stopped.
- **Load state** — relaunch restoring a saved snapshot; this **skips the boot**.
- **Open…** — load a **real ST program or disk image** and run it, so you can point
  Talos's instrumentation at demos and programs you didn't build (§3a). `.PRG` /
  `.TOS` auto-run from a GEMDOS drive; `.ST` / `.MSA` / `.STX` / `.DIM` / `.IPF`
  boot as a floppy in drive A. The button's **▾ dropdown** lists **recent files**
  (programs and disks you've opened, remembered across sessions) — pick one to
  re-load it; **Clear recent files** empties the list.
- **Disks…** — the drive **A/B disk manager**: insert / eject a floppy in either
  drive, **hot-swap** a disk on the *running* machine for a **multi-disk demo**
  (no reboot), or **boot to a clean desktop** (§3a).
- **Fast boot** (toggle with a red LED) — fast-forward an effect's ~14 s boot then
  drop back to normal speed once the effect is detected running (BUG-007). The LED
  glows red while fast boot is on; uncheck it to watch the boot at real speed.

**Run control**
- **Break** — pause the emulation. (This also turns **Live** off, so a paused
  machine is never silently resumed by the live grab.)
- **Run** — resume.
- **Step** — single-step one instruction, then refresh.
- **Refresh** — pull a fresh register snapshot + framebuffer once (handy while
  paused).
- **Live** — toggle the ~14 Hz live view. Each tick grabs a **tear-free** frame
  (it briefly fast-forwards to a frame boundary, screenshots the complete frame,
  then resumes real-time — BUG-009). On by default.
- **● Rec** — record the live view to an **animated GIF**. Toggle **on** to start
  (it turns Live on if it isn't), let the effect play, toggle **off** to choose
  where to save. It captures the same tear-free coherent frames you see, at half
  size (e.g. 416×276), looping forever, at roughly real-time cadence. ST content
  is palette-indexed so colours are preserved exactly. Recording auto-stops (and
  prompts to save) at 300 frames (~21 s); the status bar shows the running count.

**Beam parking**
- **Line** + **Run→Line** — run until the beam reaches the chosen scanline
  (0–312 PAL), then stop, with the beam overlay parked there.

**Register-write capture (M1)**
- **Reg $** — the hardware register (hex) to watch, e.g. `ffff8240` (palette 0).
- **×** — how many writes to capture (1–512).
- **Capture** — capture that many writes to the register and map each to its
  beam position; results fill the **Register-write timeline** tab.

**B2 hardware traces**
- **Depth** — max trace entries per B2 capture (1K/4K/16K/64K). Hitting the cap
  truncates the trace.
- **ms** — how long to run while tracing before dumping (100–2000 ms).
- **Blit capture** (F-208) — trace Blitter memory traffic over the run window →
  the **Blitter traffic** tab.
- **DMA capture** (F-209) — trace DMA sound drain + LMC1992 EQ over a short run
  window → the **DMA sound / EQ** tab.

### The central view — framebuffer + beam overlay

Hatari's live screen (the *taken* framebuffer, D-007), with a beam-position
overlay showing the current scanline/cycle. Geometry adapts to low/med/high-res
and PAL/NTSC. In the authoring workspaces you can **click the framebuffer** to
place a bar/band/column at that position.

### The docks / panels

- **Registers / counters** — current register values and the cycle/scanline/VBL/
  HBL counters.
- **Machine** — the selected machine's capabilities (Blitter, palette depth,
  hardware scroll, DMA sound, dual-speed) with honest ✓/✗ gating; flip machines to
  see the ST↔STE differential (F-207).
- **Palette** — the current colour palette, decoded quirk-correct for the STE
  (4096 colours, 4 bits/gun; the LSB-as-top-bit quirk, C-008), plus the per-gun
  intensity ramp.

Docks can be collapsed and are tabbed together at the bottom (see below).

### The bottom tabs

- **Register-write timeline** — the result of a **Capture**: each write to the
  watched register with its beam position; selecting a row highlights that marker
  on the framebuffer. A **scrub bar** across the top drags a cursor through the
  captured frame — the beam sweeps to the cursor and the writes light up as it
  reaches them (▶ plays the sweep). See §5, "Replay a frame in time".
- **Blitter traffic** — the F-208 Blitter trace.
- **DMA sound / EQ** — the F-209 DMA-sound drain + LMC1992 EQ view.
- **Raster workspace** — author raster bars, vertical bands, animated copper bars
  and palette colour-cycling (§6).
- **Scroller workspace** — author an STE hardware fine-scroll scroller (§7).
- **Spectrum 512** — import/convert a 512-colour picture and visualise its
  per-scanline palette storm (§7a).
- **ST picture** — view DEGAS / NEOchrome / Tiny pictures (§7b).
- **Cycle budget** — the per-scanline CPU cycle budget for the authored raster /
  bands effect, with the 8/16 MHz Mega STE budgets (§7c).
- **Border walkthrough** — the four ST borders on a 2-D screen diagram; the left
  border is runnable (§7d).
- **Sync scroll** — the STF resolution-switch fine-scroll trick, cycle→pixel table
  (§7e).
- **Reconstruct** — the taken frame beside a screen rebuilt from the captured
  register writes (F-218, §7f).
- **Disassembly** — trace the code around PC with where each instruction lands on
  the beam (§7g).
- **MFP** — the 68901 timers and interrupt controller, decoded live (§7h).
- **A/B compare** — the last-built effect on two machines side by side, with a
  per-scanline diff (§7i).

---

## 5. Common workflows

**Watch a live effect.** Launch (or `--effect <dir>` to auto-run one). Leave
**Live** on; the view refreshes tear-free. Use **Break/Run/Step** to freeze and
inspect, **Refresh** to re-pull while paused.

**See where a register write lands.** Put the register in **Reg $** (e.g. a
palette register `ffff8240`), set a count, hit **Capture**. Read the
**Register-write timeline** and click rows to see each write's beam marker.

**Replay a frame in time (scrubber).** After a capture, drag the **scrub bar** at
the top of the **Register-write timeline** tab: a cursor moves through the frame,
the beam sweeps to it, and each register write lights up on the framebuffer as the
beam reaches it (with the current write highlighted and a live line/cycle
readout). Press **▶** to play the sweep automatically; clicking a timeline row
jumps the cursor to that write. This makes the *order* an effect builds its frame
in directly visible.

**Park the beam on a scanline.** Set **Line**, hit **Run→Line**.

**Compare machines / regions.** Flip the **Machine** and **Region** combos; the
**Machine** panel shows capabilities appearing/collapsing. On a Mega STE, flip
**Clock** (8/16 MHz) and watch whether a raster effect holds or breaks (F-210).

**Snapshot / restore.** **Save state** to park the exact machine; **Load state**
to bring it back without re-booting.

---

## 6. Authoring raster effects (Raster workspace)

Open the **Raster workspace** tab. Pick a **mode**:

- **Raster bars (horizontal, per-line)** — one background colour per scanline. The
  first table column is a **scanline** (0–199); the second is the colour as
  `$0rgb` (each gun 0–7, e.g. `700` red, `070` green, `007` blue).
- **Vertical bands (intra-line, HBL-synced)** — colours packed across each line →
  vertical bands (Spectrum-512-lite). The first column is now a **framebuffer
  column (0–831)**; the lowest-column colour fills from the left edge.
- **Copper bars (animated, scrolling)** — the same bar table as Bars, but the bars
  **scroll down every frame**. Set the **scroll speed** (px/frame, the spinbox that
  appears in this mode) and Build. (Reuses the proven per-line timing — the bars
  just animate.)
- **Colour cycle (palette rotation)** — the **colour column** (up to 16 entries,
  top-to-bottom) becomes the palette, painted as a 16-index stripe ramp and
  **rotated every frame** so the colours flow across the screen.

Controls:
- **＋ Bar** / **－** — add/remove a row. You can also **click the framebuffer** to
  place a bar (Bars/Copper) or a band boundary at that column (Bands mode).
- **Fill ▾** — one-click patterns for the bar table: a **Gradient** (deep blue →
  warm white), a **Rainbow** hue-sweep, or **Mirror current** (reflect the bars
  about the centre). Handy starting points for Bars/Copper.
- Edit a colour cell (`$0rgb` hex) and the row recolours to match.
- **Build & Run** — codegen → assemble with `vasm` → relaunch Hatari on ST/PAL
  running the effect (a live preview).
- **Verify on Hatari** — run the effect headless and report pass/fail:
  `raster_roundtrip.py` (bars), `intraline_split.py` (bands), or `anim_check.py`
  (Copper/Cycle — confirms it animates; Copper also that the bars scroll). Bands
  verify needs ≥2 bands.
- **Export…** — write `raster.s` (the asm stub), `raster.json` (the portable
  register sequence) and an assembled `RASTER.PRG` to a folder you choose. The
  stub *is* the export artefact — it reproduces the effect by construction in
  stock Hatari.
- **Import…** — load a previously-exported `raster.json` back into the table (in
  the right mode) to re-edit / re-verify.

`raster.json` shape (bars): `{"effect":"raster-bars", …, "writes":[{"scanline":N,
"value":"rgb"}, …]}`; bands use `"effect":"vertical-bands"` and `"column":N`.

---

## 7. Authoring an STE hardware scroller (Scroller workspace)

Open the **Scroller workspace** tab (STE-only effect — Build previews on STE/PAL).

- **Message** — the text to scroll. With the built-in font: `A–Z 0–9` and
  `. , ! ? ' - : + /` (lowercase is uppercased; unsupported glyphs render as
  blanks). The hint line shows the character count and the resulting strip width
  in 16 px columns.
- **Font** — the built-in 8×8, or **Import font…** to load a **font-sheet image**:
  a grid of `cell`-width × `cell`-height glyphs, laid out row by row starting at
  the **from** character. A pixel is ink if it differs from the top-left
  (background) pixel, so white-on-black or black-on-white sheets both work, any
  cell size. Blank cells become spaces; the current font is shown beside the
  button. (The runnable `.PRG`/`.s` embed the rendered glyphs; a re-imported
  `scroller.json` keeps only the message + speed, re-rendered with the current
  font.)
- **Speed** — scroll speed in **pixels per frame** (1 = smoothest, up to 8).
- **Build & Run** — codegen → assemble → run on STE/PAL (live preview). The
  message is rasterised to a 1-bpp strip in Talos and scrolled by the STE shifter
  (hardware fine scroll `$ff8265`, a fixed screen base, and a fast per-wrap column
  shift of just the text band — seam-free).
- **Verify on Hatari** — run `scroller_scroll.py` headless; it confirms the
  message renders and scrolls smoothly leftward at the authored speed with no
  seam.
- **Export…** — write `scroller.s`, `scroller.json` and an assembled
  `SCROLLER.PRG` to a folder.
- **Import…** — load a `scroller.json` back (message + speed).

`scroller.json` shape: `{"effect":"ste-hardware-scroller", …, "register":"ff8265",
"speed":N, "message":"…"}`.

---

## 7a. Spectrum 512 pictures (Spectrum 512 tab)

Spectrum 512 shows up to 512 colours on an ST by rewriting the 16-register palette
~48 times per scanline. Talos **decodes and visualises** such pictures — it does
not reproduce the effect on hardware (the tightest classic ST timing trick, out of
scope). The tab has three actions:

- **Import .SPU/.SPC…** — load a real Spectrum 512 picture (uncompressed `.SPU`
  or compressed `.SPC`) and decode it exactly.
- **Import image…** — load *any* image Qt can read (PNG/JPG/BMP/GIF/…) and
  **convert** it to a Spectrum 512 picture. Detailed, colourful images convert
  well; smooth gradients band — that is S512's inherent ~10 px colour-change
  limit, not a defect.
- **Export .SPU…** — save the current picture (imported or converted) as a `.SPU`
  file that Talos (and other tools) can reload.

Below the buttons: the decoded **picture** (click a row, or use the **Scanline**
spinner, to inspect it) and the **palette storm** for the selected line — the
resolved scanline, where each of the 16 registers flips set 1 → set 2 (at
`x = 1,5,21,25,…`) and set 2 → set 3 (160 px later) across the beam, and the three
16-colour palette sets. Sample pictures are user-supplied (copyright, like TOS).

---

## 7b. ST pictures (ST picture tab)

The **ST picture** tab views the classic 16-colour ST picture formats — one
palette per screen, unlike the Spectrum 512 palette storm. **Import ST picture…**
loads and decodes:

- **DEGAS** — `.PI1/.PI2/.PI3` (uncompressed) and `.PC1/.PC2/.PC3` (Degas Elite,
  PackBits-compressed).
- **NEOchrome** — `.NEO`.
- **Tiny** — `.TNY/.TN1/.TN2/.TN3` (RLE-compressed).

All three ST resolutions decode: low (320×200, 16 colours), medium (640×200 → 400
for correct aspect, 4 colours) and high (640×400, mono). The decoded picture is
shown scaled to fit, with the file's palette as swatches below. The decoders were
validated pixel-for-pixel against the canonical RECOIL decoder. Sample pictures
are user-supplied (copyright).

---

## 7c. Scanline cycle budget (Cycle budget tab)

One ST scanline is a fixed number of CPU cycles — **512** at 8 MHz PAL 50 Hz
(**508** NTSC), sourced from Hatari, never hand-counted. That is the budget for
all of an effect's per-line work. The **Cycle budget** tab draws one scanline as a
gauge and updates live as you edit the **Raster workspace**:

- the **visible display** window is shaded (the border/blanking regions flank it);
- the effect's per-line register writes are placed on the line — **green** inside
  budget, **red** if a write falls past the visible edge (it won't show). Raster
  **bars** are one write/line, cycle-locked to 512 by the codegen's pad; **bands**
  plot each band boundary at its cycle position;
- on a dual-speed **Mega STE**, the **16 MHz** budget (1024 cycles/line) is drawn
  past the 8 MHz line — which is why an effect that overflows at 8 MHz can still
  hold at 16 MHz (this is the F-210 dual-speed story, made concrete).

---

## 7d. Border-removal walkthrough (Border walkthrough tab)

A guided tour of the four ST screen borders — the trick that turns a border into
extra screen. Pick **Left / Right / Top / Bottom** and the tab shows, on one 2-D
**screen diagram** (X = cycles across a scanline, Y = lines down the frame):

- the normal **display rectangle** (cycles 56–376 × lines 63–263, PAL);
- the region that border **opens** (green) — left/right widen the *line*,
  top/bottom add *lines* to the *frame*;
- the **switch marker**: for left/right a vertical line at the cycle the write
  must hit (every line); for top/bottom a horizontal line at the trick scanline
  with a tick at cycle **504** (the deadline).

Below the diagram, the **facts panel** gives the exact **register** and write
(`$ffff8260` hi/lo for the left border; `$ffff820a` 50↔60 Hz for the others), the
**cycle window**, which **line(s)** it runs on, and the **consequence** (left
~52 px, right +44 px, top +29 lines, bottom +47 lines in 50 Hz). Every figure is
sourced from Hatari's `video.h`/`video.c` — never from memory (C-007).

The **Left border** is runnable: **Build & Run** codegens a cycle-exact
`$ffff8260` hi/lo switch, assembles it and launches on ST/PAL so you watch the
left border open live; **Verify on Hatari** runs it headless and confirms the
border opens on a band of scanlines (the `diff_harness.py --border-check` path).
The other three are teaching views — the mechanism, window and consequence are
shown, but opening them cycle-exact live is left as the documented recipe.

---

## 7e. Sync-scroll walkthrough (Sync scroll tab)

The plain STF has no fine-scroll register (that is an STE feature). ST Connexion's
answer — the **sync scroll** — is three **`$ffff8260`** resolution switches at the
*start* of a scanline: **hi-res ($02)** at LineCycles ≤ 4, **med-res ($01)** at
≤ 20, then **lo-res ($00)** at an **exact** cycle that sets the pixel shift:

| lo-res switch cycle | right shift |
|---|---|
| 16 | 0 px (remove-left + med stabiliser) |
| 20 | 13 px |
| 24 | 9 px |
| 28 | 5 px |
| 32 | 1 px |

Pick a shift and the tab shows the three switches on a zoomed **line-start
timeline** (the ≤4 and ≤20 windows shaded), the sourced **cycle→pixel table**, and
a **before/after** strip illustrating the shift. Combine the fine shift with a
byte-granular screen-address change (the coarse 16 px step) to build a smooth
scroll — the STF's answer to the STE hardware scroller. Every figure is sourced
from Hatari `video.h`/`video.c` (C-007).

This is a **teaching view**: the trick needs the low-res write to land on an
*exact* cycle (not a window), which Talos documents rather than reproduces live —
there is no bench-proven stub for it, unlike the runnable left border.

---

## 7f. Reconstruct from registers (Reconstruct tab — F-218)

A **secondary** teaching view: Hatari's taken framebuffer beside a screen rebuilt
**purely from the captured register writes**, so you see *why* the picture looks
as it does — and where the register field and reality diverge. It never replaces
the taken frame (D-007).

1. Set **Reg $** to a palette register — **`ff8240`** (background / palette 0) is
   the useful one — and **Capture** a band of writes (see §4) on a running raster
   or vertical-band effect.
2. Open the **Reconstruct** tab. The right panel folds those writes onto one
   frame by their beam position (scanline + cycle) and paints each pixel with the
   palette colour in effect there — reconstructing the effect's background colour
   field. The left panel is Hatari's real frame for comparison.

The reconstruction is meaningful for **palette-register** captures
(`$ff8240`–`$ff825e`); other registers show a note. Talos does not emulate here —
it decodes the captured values with the same ST/STE palette rules as the real
machine (the STE bit-order quirk included) and places them with the beam
geometry, so a raster-bars capture rebuilds as horizontal colour bands and a
vertical-band capture as vertical ones.

---

## 7g. Live disassembly synced to the beam (Disassembly tab)

Where the register-write capture shows *when* a chosen register is written, this
shows the **instruction stream** and where each instruction lands on the beam —
re-aiming a debugger's disassembly at "this write happens here".

1. **Break** in the code you want to read — ideally an effect's per-scanline loop
   (Break, or Run→Line to a visible scanline, then Break).
2. Open the **Disassembly** tab, set **Trace** to a count, and **Trace from PC**.

Talos single-steps that many instructions from PC, and for each records — from
**Hatari**, never estimated (D-002) — its disassembly, the beam position
(**Line** = scanline, **Cycle** = cycle-in-line) it was about to run at, and its
**Cost** in cycles (how far the beam advanced). Instructions that **write a video
register** (`$ff82xx` — palette, sync, resolution, scroll) are **highlighted** —
the payoff rows. Selecting any row **parks the beam overlay** on the framebuffer
at that instruction's position, so you see exactly where on the screen it fires.

(Tracing steps the machine forward and pauses the live view; press **Run**/**Live**
to resume. The disassembly is Hatari's own, fetched via its console.)

---

## 7h. MFP timers & interrupts (MFP tab)

The MC68901 **MFP** runs the ST's four hardware timers and its interrupt
controller — Timer-B drives Spectrum 512, Timer-C is the 200 Hz system tick, and
effects lean on the timers and the HBL. The **MFP** tab reads the register block
(`$fffa00`–`$fffa2f`) from the live machine and decodes it:

- **Timer table** (A–D): the **mode** (stopped / delay / event count / pulse),
  **prescaler** (`/4`…`/200`), **data** register, computed **frequency**
  (XTAL 2.4576 MHz ÷ prescaler ÷ data) and **interrupts per frame** — e.g. a
  running Timer-C shows *200 Hz · 4/frame (~every 78 lines)*. Running timers are
  green; Timer-B in **event count** mode fires per counted event (display line).
- **Interrupt matrix** (16 sources, highest priority first): **En** (enabled,
  IER), **Msk** (unmasked, IMR), **Pnd** (pending, IPR), **Svc** (in-service,
  ISR). Timer rows are highlighted; a source that is enabled *and* unmasked is
  the one that will actually interrupt the CPU.

Press **Read MFP** to refresh (it also refreshes on a manual **Refresh**). Every
figure is decoded from the registers with constants sourced from Hatari
(`mfp.c` / `clocks_timings.c`, C-007) — Talos reads the MFP, it does not emulate it.

---

## 7i. A/B machine comparison (A/B compare tab)

Runs the **last-built effect** on two machines side by side and shows where they
diverge — the STE's brighter palette or its prefetch shift changing an
ST-authored effect, or an STE-only effect that is blank on a plain ST. Extends the
F-207 ST↔STE differential to whole frames.

1. Build an effect first (Raster / Scroller / Border — it sets the compared
   program).
2. Open the **A/B compare** tab, pick **A** and **B** machines (e.g. *520/1040 ST*
   vs *STE*), and **Compare**.

Talos captures a frame of the effect on each machine **headless** (each synced to
the same VBL, via `harness/ab_compare.py`) and shows them side by side with a
**Δ strip** — a red mark on every scanline where the two frames differ — and a
count. A machine-agnostic effect reads *Identical*; a raster effect using STE-only
bright colours (bit 3 of a gun nibble) shows *all scanlines differ* because a
plain ST masks those colours darker (the C-008 palette quirk). Talos captures and
diffs; it emulates nothing.

---

## 8. Verify harnesses (command line)

The `harness/` scripts run Hatari headless and check an effect. They need
`python3` with `numpy` + `Pillow`. The client's **Verify** buttons call these for
you, but you can run them directly:

```bash
# The whole regression corpus in one command — determinism/non-perturbation,
# left-border removal, raster-bar round-trip, vertical bands and the STE scroller.
# Prints a PASS/FAIL table and exits non-zero if any check fails (~70 s).
harness/corpus.sh                  # or: harness/corpus.sh --only border

# Per-scanline determinism / non-perturbation vs stock Hatari (M1)
harness/run.sh                     # wraps diff_harness.py with repo defaults

# Raster-bar round-trip: codegen -> assemble -> run -> confirm the bands
harness/raster_roundtrip.py --hatari <bin> --tos <rom> --bar 20:700 --bar 100:070

# Intra-line vertical split / arbitrary-column calibration
harness/intraline_split.py  --hatari <bin> --tos <rom> --cols 300,450,600
harness/intraline_split.py  --hatari <bin> --tos <rom> --sweep   # recalibrate

# STE scroller: assemble the client-generated stub, confirm it scrolls seam-free
harness/scroller_scroll.py  --hatari <bin> --tos <rom> --asm scroller.s --speed 1
```

See `harness/README.md` for the details of each, including the two Hatari capture
gotchas they must respect: fast-forward **frame-skips rendering** (use
`--frameskips 0`, or grab at a frame boundary) and the VBL breakpoint count
**races past boot** (target a VBL relative to / above the live count).

---

## 9. Gotchas & tips

- **You must supply a TOS image** (C-009). A multi-language EmuTOS is required for
  the region/language selectors to do anything.
- **`vasm` is required** for the Raster/Scroller **Build**, **Verify** and
  **Export** actions. If it's missing, run `scripts/bootstrap-vasm.sh` — the
  workspace result line will tell you.
- **The Scroller is STE-only** (hardware fine scroll). Build previews it on STE
  regardless of the current machine selector.
- **Fast boot** makes an effect appear in ~0.6 s instead of ~14 s; turn it off to
  watch the actual boot.
- **Live view runs ~14 Hz** (down from 20 Hz) because each frame is grabbed
  coherently to avoid tearing on animated effects (BUG-009). That's expected.
- Exported artefacts (`raster.s/json`, `scroller.s/json`, `*.PRG`) and state
  snapshots (`*.sav`) are gitignored — they're outputs, not source.

---

## 10. Where to read more

- `README.md` — what Talos is and why.
- `CLAUDE.md` — cold-start brief; current build state and where to pick up.
- `ARCHITECTURE.md` — the two-process design and the B1/B2 instrumentation split.
- `DECISIONS.md` — the ten decisions and their reversal conditions.
- `REGISTERS.md` — features (F-NNN) and constraints (C-NNN).
- `BUGS.md` — known issues and by-design limitations (BUG-NNN).
- `harness/README.md` — the validation harnesses in detail.
