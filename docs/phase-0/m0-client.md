# Phase 0 — M0 Qt client

> **Status:** Active
> **Provenance:** Claude (implementer), built and verified headless on the dev ThinkPad 2026-07-10.
> **Last reviewed:** 2026-07-10
> **Why this status:** M0 exit criterion met and verified end to end (headless). Not yet exercised on a physical display; the GUI wiring is verified via the offscreen `--selftest` path, which drives the real MainWindow slots.

---

M0 = the thinnest end-to-end pipe with a Qt client (ROADMAP Phase 0). All B1 —
no fork patches.

## What it does

A Qt6 app (`client/`, target `talos`) that:

- launches (or attaches to) the Hatari fork over the remote socket (F-214);
- reads registers + the video beam/cycle counters via `regs` (F-215, minimal);
- drives run / stop / step;
- takes Hatari's rendered framebuffer via `console screenshot` and displays it,
  overlay-ready (D-007);
- refreshes live (~4 Hz) while connected.

## Components (`client/src/`, per ARCHITECTURE §5)

| Dir | Class | Role |
|---|---|---|
| `protocol/` | `RdbClient` | socket + framing (0x1 sep / 0x0 term); routes `!`-notifications vs `OK`/`NG` replies through a FIFO |
| `model/` | `MachineState` | parses a `regs` reply to name→value; typed beam-counter accessors |
| `session/` | `HatariLauncher` | `QProcess` launch (headless option) / attach |
| `view/` | `FramebufferView` | paints the taken PNG frame, aspect-correct, crisp pixels |
| `app/` | `MainWindow` | toolbar (Break/Run/Step/Refresh/Live), register panel, framebuffer, status bar |

## Build & run

```bash
cmake --preset debug && cmake --build --preset debug
# GUI (needs a display); defaults find external/hatari + tos/etos512uk.img:
./build/debug/bin/talos
# explicit:
./build/debug/bin/talos --hatari <path/hatari> --tos <path/rom> --machine st
# attach to an already-running Hatari instead of launching one:
./build/debug/bin/talos --attach
```

## Headless self-test (CI-friendly, D-009 spirit)

`--selftest <png>` auto-starts a session, captures one taken frame, saves it, and
exits 0 (2 on timeout, 3 on save failure). Used to verify M0 without a display:

```bash
QT_QPA_PLATFORM=offscreen ./build/debug/bin/talos --headless \
  --selftest /tmp/frame.png --hatari external/hatari/build/src/hatari \
  --tos tos/etos512uk.img
```

Verified 2026-07-10: captured EmuTOS 1.4 boot screen (832×552), `step` advances
PC (`$E1FE0A → $E0086E → $E00872`), clean exit 0.

## Known limits (M0 scope)

- Framebuffer refresh is screenshot-poll (file round-trip), not a live stream. Fine
  for M0; Phase 3 revisits if a per-frame stream is needed (C-004 / D-004).
- `console screenshot` runs on the request/reply path; at high refresh rates it
  adds socket traffic. The ~4 Hz Live timer is deliberately modest.
- Notifications are surfaced minimally; full `!config`/`!status` parsing is later.
- GUI not yet run on a physical display — only the offscreen self-test path.
