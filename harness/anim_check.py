#!/usr/bin/env python3
"""anim_check.py — verify a client-generated animated raster stub actually moves.

Assembles the .s, runs it headless, captures two frames a few VBLs apart, and
confirms they differ (the effect animates). For --mode copper it also requires a
vertical shift (the bars scroll); for --mode cycle a plain frame difference (the
palette rotates) is enough.

  anim_check.py --hatari <bin> --tos <rom> --asm <fx.s> --mode copper|cycle
"""
import argparse, os, signal, subprocess, sys, tempfile, time
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(__file__))
import diff_harness as dh

V1, V2 = 3000, 3011   # capture VBLs (11 frames apart — coprime to small cycle periods)


def capture_two(hatari, tos, effdir):
    cfg = os.path.join(tempfile.gettempdir(), "talos_anim.cfg")
    open(cfg, "w").close()
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    p = subprocess.Popen(
        [hatari, "--configfile", cfg, "--tos", tos, "--machine", "st", "--sound", "off",
         "--statusbar", "off", "--fast-forward", "on", "-d", effdir],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, preexec_fn=os.setsid)
    try:
        r = dh.connect_retry()
        r.cmd("break"); r.drain_greeting(); r.wait_stopped()
        v0 = r.regs().get("VBL", 0)
        base = V1 if v0 < V1 - 50 else v0 + 50
        f = []
        for target in (base, base + (V2 - V1)):
            r.run_to_vbl(target)
            png = os.path.join(tempfile.gettempdir(), f"talos_anim_{target}.png")
            r.cmd(f"console screenshot {png}"); time.sleep(0.35)
            f.append(np.asarray(Image.open(png).convert("RGB")).astype(int))
        return f[0], f[1]
    finally:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except ProcessLookupError:
            pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hatari", required=True)
    ap.add_argument("--tos", required=True)
    ap.add_argument("--asm", required=True)
    ap.add_argument("--mode", default="copper")
    ap.add_argument("--vasm", default=os.path.join(os.path.dirname(__file__),
                                                   "../external/vasm-src/vasm/vasmm68k_mot"))
    a = ap.parse_args()

    outdir = tempfile.mkdtemp(prefix="anim_")
    os.makedirs(os.path.join(outdir, "AUTO"))
    r = subprocess.run([os.path.abspath(a.vasm), "-Ftos", "-quiet",
                        "-o", os.path.join(outdir, "AUTO", "FX.PRG"), a.asm],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print("RESULT: FAIL — vasm:", r.stderr[:200])
        return 1

    f1, f2 = capture_two(a.hatari, a.tos, outdir)
    diff = float(np.abs(f1 - f2).mean())
    animated = diff > 3.0

    shifted = True
    if a.mode == "copper":
        x = f1.shape[1] // 2
        ca, cb = f1[:, x, :], f2[:, x, :]
        best_k, best_d = 0, 1e9
        for k in range(-60, 61, 2):
            d = float(np.abs(ca - np.roll(cb, k, axis=0)).mean())
            if d < best_d:
                best_k, best_d = k, d
        shifted = best_k != 0
        print(f"  frame-diff={diff:.1f}, vertical shift={best_k}px")
    else:
        print(f"  frame-diff={diff:.1f}")

    ok = animated and shifted
    print("RESULT:", "PASS — the effect animates" if ok else "FAIL — no motion detected")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
