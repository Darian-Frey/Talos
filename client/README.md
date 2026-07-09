# Talos client (Qt6 / C++)

> **Status:** Active
> **Provenance:** Claude (implementer), Phase 0 scaffold.
> **Last reviewed:** 2026-07-09
> **Why this status:** Directory layout established; `src/main.cpp` is a pre-M0 placeholder window. Components fill in from M0 onward.

---

The Talos application (D-006). It contains **no emulation** (C-001) — it drives the
Hatari fork over the remote socket and visualises what comes back.

## Component layout (`src/`, per ARCHITECTURE §5)

| Dir | Role | Feature |
|---|---|---|
| `app/` | Application shell, main window, panel docking | — |
| `session/` | Owns the socket; launches/attaches to the fork; negotiates protocol version + per-tap capabilities; degrades when a B2 tap is absent | F-214 |
| `protocol/` | Client-side implementation of the wire contract in `../../protocol/` (B1 now, B2 later) | F-201 |
| `model/` | Per-frame ring of cycle/scanline-indexed events every panel reads from | F-215 |
| `view/` | Framebuffer + beam/write overlays, register-write timeline, register panels, differential + subsystem views | F-202–F-210 |

## Build

```
cmake --preset debug          # from repo root
cmake --build --preset debug
./build/debug/bin/talos
```

Requires Qt6 (Core, Gui, Widgets, Network). The Hatari fork is built separately
via `scripts/bootstrap-hatari.sh`.
