#!/usr/bin/env python3
"""corpus.py — run the whole effect-regression corpus against stock Hatari.

One command that drives every per-effect harness in turn (each launches its own
headless Hatari on the fixed remote port, so they run sequentially) and reports a
pass/fail summary. This is the "one production per hard trick" regression net:
raster bars, the left border, intra-line vertical bands and the STE scroller each
reproduce, and a driven run stays identical to stock Hatari (determinism /
non-perturbation, D-009).

Usage:
  corpus.py                       # repo defaults for --hatari / --tos
  corpus.py --only border         # run only checks whose name matches
Exit 0 = whole corpus passed, 1 = at least one check failed.
"""
import argparse
import os
import subprocess
import sys
import time

HARN = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HARN)


def main():
    ap = argparse.ArgumentParser(description="Talos effect-regression corpus")
    ap.add_argument("--hatari", default=os.path.join(REPO, "external/hatari/build/src/hatari"))
    ap.add_argument("--tos", default=os.environ.get("TALOS_TOS",
                                                    os.path.join(REPO, "tos/etos512uk.img")))
    ap.add_argument("--only", help="run only checks whose name contains this substring")
    args = ap.parse_args()

    hb = ["--hatari", args.hatari, "--tos", args.tos]
    P = lambda s: os.path.join(HARN, s)
    E = lambda s: os.path.join(REPO, "tests/effects", s)

    corpus = [
        ("determinism / non-perturbation",
         ["python3", P("diff_harness.py"), *hb, "--effect", E("disk")]),
        ("left-border removal",
         ["python3", P("diff_harness.py"), *hb, "--effect", E("disk-lborder"), "--border-check"]),
        ("raster-bars round-trip",
         ["python3", P("raster_roundtrip.py"), *hb,
          "--bar", "20:700", "--bar", "90:070", "--bar", "150:007"]),
        ("vertical bands (Spectrum-lite)",
         ["python3", P("intraline_split.py"), *hb, "--multi", "--nbands", "8"]),
        ("copper bars (animated)",
         ["python3", P("anim_check.py"), *hb, "--asm", E("copper.s"), "--mode", "copper"]),
        ("palette colour-cycling",
         ["python3", P("anim_check.py"), *hb, "--asm", E("cycle.s"), "--mode", "cycle"]),
        ("STE hardware scroller",
         ["python3", P("scroller_scroll.py"), *hb, "--asm", E("corpus-scroller.s"),
          "--speed", "1", "--message", "corpus"]),
    ]

    results = []
    for name, argv in corpus:
        if args.only and args.only.lower() not in name.lower():
            continue
        print(f"\n=== {name} ===", flush=True)
        t0 = time.time()
        r = subprocess.run(argv, capture_output=True, text=True)
        dt = time.time() - t0
        out = r.stdout + r.stderr
        summary = ""
        for ln in out.splitlines():
            if any(k in ln for k in ("RESULT", "HARNESS", "BORDER CHECK")):
                summary = ln.strip()
        ok = r.returncode == 0
        results.append((name, ok, summary, dt))
        print(summary or out.strip()[-300:], flush=True)

    print("\n" + "=" * 68)
    print("%-34s %-6s %6s  %s" % ("CHECK", "RESULT", "TIME", "detail"))
    print("-" * 68)
    all_ok = True
    for name, ok, summary, dt in results:
        all_ok = all_ok and ok
        print("%-34s %-6s %5.0fs  %s" % (name, "PASS" if ok else "FAIL", dt, summary[:34]))
    print("=" * 68)
    print("CORPUS PASS (%d/%d)" % (len(results), len(results)) if all_ok
          else "CORPUS FAIL (%d/%d passed)" % (sum(1 for _, ok, _, _ in results if ok), len(results)))
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
