# Talos — User Manual

> **Status:** Active
> **Provenance:** Claude (implementer), written 2026-07-16 from the client source (`client/`) and harnesses (`harness/`).
> **Last reviewed:** 2026-07-16
> **Why this status:** Practical how-to for the client through Phase 4 (M0–M4 + F-217). For the design rationale read `CLAUDE.md`, `ARCHITECTURE.md` and `DECISIONS.md`; for the authoritative feature/bug state read `REGISTERS.md`, `BUGS.md` and `git log`.

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

## 4. The interface

### The toolbar (top, left → right)

**Session / machine**
- **Machine** — 520/1040 ST · Mega ST · STE · Mega STE. Changing it relaunches
  Hatari on that machine.
- **Language** — EmuTOS language (also sets the country/region EmuTOS boots in).
- **Region** — video region, 50 Hz PAL / 60 Hz NTSC. Talos also *reads back* the
  real region from `$ff820a`, so the overlay geometry follows reality even if an
  effect flips sync mode.
- **Clock** — CPU clock 8/16 MHz. Only meaningful on the dual-speed **Mega STE**
  (F-210); disabled otherwise.
- **Launch** (or **Connect** with `--attach`) — start Hatari + connect.
- **Stop** — stop the running machine and disconnect.

**State (F-217)**
- **Save state** — snapshot the whole machine to a file (park a prototype, or seed
  a validation run). Works while running or stopped.
- **Load state** — relaunch restoring a saved snapshot; this **skips the boot**.
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
  on the framebuffer.
- **Blitter traffic** — the F-208 Blitter trace.
- **DMA sound / EQ** — the F-209 DMA-sound drain + LMC1992 EQ view.
- **Raster workspace** — author raster-bar / vertical-band effects (§6).
- **Scroller workspace** — author an STE hardware fine-scroll scroller (§7).

---

## 5. Common workflows

**Watch a live effect.** Launch (or `--effect <dir>` to auto-run one). Leave
**Live** on; the view refreshes tear-free. Use **Break/Run/Step** to freeze and
inspect, **Refresh** to re-pull while paused.

**See where a register write lands.** Put the register in **Reg $** (e.g. a
palette register `ffff8240`), set a count, hit **Capture**. Read the
**Register-write timeline** and click rows to see each write's beam marker.

**Park the beam on a scanline.** Set **Line**, hit **Run→Line**.

**Compare machines / regions.** Flip the **Machine** and **Region** combos; the
**Machine** panel shows capabilities appearing/collapsing. On a Mega STE, flip
**Clock** (8/16 MHz) and watch whether a raster effect holds or breaks (F-210).

**Snapshot / restore.** **Save state** to park the exact machine; **Load state**
to bring it back without re-booting.

---

## 6. Authoring raster bars & vertical bands (Raster workspace)

Open the **Raster workspace** tab. Pick a **mode**:

- **Raster bars (horizontal, per-line)** — one background colour per scanline. The
  first table column is a **scanline** (0–199); the second is the colour as
  `$0rgb` (each gun 0–7, e.g. `700` red, `070` green, `007` blue).
- **Vertical bands (intra-line, HBL-synced)** — colours packed across each line →
  vertical bands (Spectrum-512-lite). The first column is now a **framebuffer
  column (0–831)**; the lowest-column colour fills from the left edge.

Controls:
- **＋ Bar** / **－** — add/remove a row. You can also **click the framebuffer** to
  place a bar (Bars mode) or a band boundary at that column (Bands mode).
- Edit a colour cell (`$0rgb` hex) and the row recolours to match.
- **Build & Run** — codegen → assemble with `vasm` → relaunch Hatari on ST/PAL
  running the effect (a live preview).
- **Verify on Hatari** — run the exported stub through the headless round-trip
  harness (`raster_roundtrip.py` for bars, `intraline_split.py` for bands) and
  report pass/fail. Bands verify needs ≥2 bands.
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

- **Message** — the text to scroll. Supports `A–Z 0–9` and `. , ! ? ' - : + /`
  (lowercase is uppercased; unsupported glyphs render as blanks). The hint line
  shows the character count and the resulting strip width in 16 px columns.
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

## 8. Verify harnesses (command line)

The `harness/` scripts run Hatari headless and check an effect. They need
`python3` with `numpy` + `Pillow`. The client's **Verify** buttons call these for
you, but you can run them directly:

```bash
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
