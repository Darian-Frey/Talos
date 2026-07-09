# Talos — B2 fork patches

> **Status:** Active
> **Provenance:** Claude (implementer), Phase 0 scaffold.
> **Last reviewed:** 2026-07-09
> **Why this status:** Empty by design. No B2 patch exists until a feature proves B1 cannot serve it (D-005). First candidates land in Phase 3.

---

The **only** part of the Hatari fork tracked in this repo. The fork itself is
cloned into `external/` and never vendored (GPLv2 upstream, separate build).

Each patch here is an instrumentation **tap** (ARCHITECTURE §3) plus the new
protocol packet that carries its data out. Rules:

- **One patch per feature**, justified by a specific visualisation B1 cannot
  supply (C-002). Name them so the feature is obvious, e.g.
  `0001-tap-register-write-cycle.patch`.
- **Keep them surgical and rebasable** — every patch widens the gap from
  mainline and the rebase cost (C-002). Minimise the seam touched.
- **Confirm tap points against real source first** (C-003) — the candidate
  points in ARCHITECTURE §3 are indicative until read in the fork.

Maintained as an ordered series applied on top of the pinned fork commit
(see `scripts/bootstrap-hatari.sh`). No patches yet.
