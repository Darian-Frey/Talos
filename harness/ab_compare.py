#!/usr/bin/env python3
"""ab_compare.py — run the same effect on two machines and diff the frames.

Captures a frame of --effect on --machine-a and on --machine-b (each headless,
synced to a fixed VBL), writes them to --out-a / --out-b, and reports the
per-scanline difference — where the two machines diverge (e.g. an ST-timed effect
breaking on the STE because of its extra prefetch shift). Backs the client's A/B
comparison view (extends the F-207 ST<->STE differential).

Usage:
  ab_compare.py --hatari <bin> --tos <rom> --effect <dir> \
    --machine-a st --machine-b ste --out-a a.png --out-b b.png
"""
import argparse, os, signal, subprocess, sys, tempfile, time
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(__file__))
import diff_harness as dh


def run_machine(args, machine, out_png):
    cfg = os.path.join(tempfile.gettempdir(), "talos_ab_empty.cfg")
    open(cfg, "w").close()
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    proc = subprocess.Popen(
        [args.hatari, "--configfile", cfg, "--tos", args.tos, "--machine", machine,
         "--sound", "off", "--statusbar", "off", "--fast-forward", "on", "-d", args.effect],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, preexec_fn=os.setsid)
    try:
        rdb = dh.connect_retry()
        rdb.cmd("break"); rdb.drain_greeting(); rdb.wait_stopped()
        v0 = rdb.regs().get("VBL", 0)
        if v0 >= dh.SHOT_VBL:
            raise RuntimeError(f"connected too late (VBL {v0})")
        rdb.run_to_vbl(dh.SHOT_VBL)
        rdb.screenshot(out_png)
    finally:
        dh.kill(proc)
    return np.asarray(Image.open(out_png).convert("RGB"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hatari", required=True)
    ap.add_argument("--tos", required=True)
    ap.add_argument("--effect", required=True)
    ap.add_argument("--machine-a", default="st")
    ap.add_argument("--machine-b", default="ste")
    ap.add_argument("--out-a", required=True)
    ap.add_argument("--out-b", required=True)
    args = ap.parse_args()

    print(f"A/B compare — {args.effect}: {args.machine_a} vs {args.machine_b}")
    a = run_machine(args, args.machine_a, args.out_a)
    b = run_machine(args, args.machine_b, args.out_b)
    if a.shape != b.shape:
        print(f"RESULT: DIFFERENT (frame size {a.shape} vs {b.shape})")
        return 0
    row_diff = np.any(a != b, axis=(1, 2))
    ndiff = int(row_diff.sum())
    rows = np.nonzero(row_diff)[0]
    span = f"{int(rows.min())}..{int(rows.max())}" if rows.size else "-"
    print(f"frame {a.shape[1]}x{a.shape[0]}; {ndiff}/{a.shape[0]} scanlines differ (rows {span})")
    print("RESULT: IDENTICAL" if ndiff == 0 else "RESULT: DIVERGES")
    return 0


if __name__ == "__main__":
    sys.exit(main())
