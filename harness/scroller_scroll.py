#!/usr/bin/env python3
"""Talos Phase 4 (F-212): verify an STE hardware fine-scroll scroller.

Assembles the client-generated stub (scroller.s), runs it headless on an STE,
and confirms the message renders and scrolls smoothly leftward at the authored
speed with no seam. Because the stub *is* the export artefact, running it in
stock Hatari reproduces the effect by construction; this is the automated
"verify on Hatari".

Robust metric (sparse text defeats naive per-frame cross-correlation at the
16 px wrap, where new glyphs enter): correlate frame 0 against frames 1..K.
Same content, just shifted, so each lag should be ~ -speed*2*k (2 screen px per
ST px). A real seam breaks that linearity; sparse-text correlation noise does
not. PASS = text present AND the shift is linear with the expected slope.

Measurement traps (see ScrollerCodegen.cpp): Hatari fast-forward frame-skips
rendering, so --frameskips 0 is mandatory; and its VBL breakpoint count races
past boot, so we target a VBL relative to the live count, not an absolute one.

Usage:
  scroller_scroll.py --hatari <bin> --tos <rom> --asm <scroller.s> --speed N
"""
import argparse, os, signal, socket, subprocess, sys, tempfile, time
import numpy as np
from PIL import Image

RDB_PORT = 56001


class Rdb:
    def __init__(s):
        s.s = socket.create_connection(("127.0.0.1", RDB_PORT), timeout=5)
        s.s.settimeout(30); s.b = bytearray()
    def _m(s):
        while True:
            i = s.b.find(0)
            if i >= 0:
                v = bytes(s.b[:i]); del s.b[:i+1]; return v.split(b"\x01")
            c = s.s.recv(1 << 20)
            if not c: return None
            s.b += c
    def cmd(s, l):
        s.s.sendall(l.encode() + b"\0")
        while True:
            m = s._m()
            if m is None: return None
            if m and m[0] in (b"OK", b"NG"): return m
    def drain(s):
        s.s.settimeout(0.4)
        try:
            while True: s.s.recv(1 << 16)
        except socket.timeout: pass
        s.s.settimeout(30)
    def wait(s, t=4000):
        for _ in range(t):
            st = s.cmd("status")
            if st and len(st) > 1 and st[1] == b"0": return True
            time.sleep(0.01)
    def cur_vbl(s):
        m = s.cmd("regs")
        if not m: return None
        for i, t in enumerate(m):
            if t == b"VBL":
                for u in m[i+1:]:
                    if u: return int(u, 16)
        return None
    def to_vbl(s, v):
        s.cmd("break"); s.wait(); s.cmd(f"bp VBL = {v} :once"); s.cmd("run"); s.wait()
        return s.cur_vbl()


def assemble(vasm, asm_path, outdir):
    auto = os.path.join(outdir, "AUTO")
    os.makedirs(auto, exist_ok=True)
    r = subprocess.run([vasm, "-Ftos", "-o", os.path.join(auto, "S.PRG"), asm_path],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("vasm failed:\n" + r.stdout + r.stderr)


def lag(a, b, maxlag=50):
    """Best integer lag shifting b onto a (screen px); +ve = b moved right."""
    a = a - a.mean(); b = b - b.mean()
    return max(range(-maxlag, maxlag + 1),
               key=lambda L: float((a * np.roll(b, L)).sum()))


SCROLL_VBL = 3000        # sample well after boot, effect running (cf. raster harness)


def run_and_measure(hatari, tos, effdir, nframes=14):
    cfg = os.path.join(tempfile.gettempdir(), "scroller_v.cfg"); open(cfg, "w").close()
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    p = subprocess.Popen(
        [hatari, "--configfile", cfg, "--tos", tos, "--machine", "ste",
         "--sound", "off", "--statusbar", "off", "--fast-forward", "on",
         "--frameskips", "0", "-d", effdir],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, preexec_fn=os.setsid)
    try:
        r = None
        for _ in range(150):
            try: r = Rdb(); break
            except OSError: time.sleep(0.15)
        r.cmd("break"); r.drain(); r.wait()
        # Boot completes well before SCROLL_VBL; if fast-forward already raced
        # past it, fall back to a little ahead of the live count.
        cur = r.cur_vbl() or 0
        start = SCROLL_VBL if cur < SCROLL_VBL - nframes else cur + 40
        imgs, land = [], []
        for k in range(nframes):
            land.append(r.to_vbl(start + k))
            png = os.path.join(effdir, f"f{k}.png")
            r.cmd(f"console screenshot {png}"); time.sleep(0.3)
            imgs.append(np.asarray(Image.open(png).convert("RGB")))
        return imgs, land
    finally:
        try: os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except ProcessLookupError: pass


def verify(imgs, land, speed):
    # yellow text (colour 1) on blue bg (colour 0): row with the most text = band.
    def ymask(img): return np.logical_and(img[:, :, 0] > 150, img[:, :, 2] < 120)
    npix = int(ymask(imgs[0]).sum())
    row = int(ymask(imgs[0]).sum(axis=1).argmax())
    sig = [img[row].astype(float).sum(axis=1) for img in imgs]

    # frame 0 vs frame k: same content shifted left by speed*k ST px, so the lag
    # grows +2*speed per frame (2 screen px per ST px; leftward is +ve here).
    ks, lags = [], []
    for k in range(1, len(imgs)):
        if land[0] is None or land[k] != land[0] + k:
            continue                     # drop any non-consecutive landing
        ks.append(k); lags.append(lag(sig[0], sig[k]))
    if len(ks) < 6:
        return False, npix, "too few consecutive frames", ks, lags
    # Robust to a couple of sparse-text correlation glitches at the wrap: take the
    # median slope, then require nearly every point on that line (a real seam
    # would knock a run of points off it, not just one).
    expect = 2.0 * speed
    slope = float(np.median([l / k for k, l in zip(ks, lags)]))
    inliers = sum(1 for k, l in zip(ks, lags) if abs(l - slope * k) <= 4.0)
    slope_ok = abs(slope - expect) <= 0.6              # ~0.3 ST px/frame of the target
    linear_ok = inliers >= len(ks) - 2                 # allow <=2 glitch frames
    ok = npix > 200 and slope_ok and linear_ok
    why = (f"slope={slope:.2f} (want {expect:.0f}), inliers={inliers}/{len(ks)}, "
           f"text_px={npix}")
    return ok, npix, why, ks, lags


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hatari", required=True)
    ap.add_argument("--tos", required=True)
    ap.add_argument("--asm", required=True, help="client-generated scroller.s")
    ap.add_argument("--speed", type=int, default=1)
    ap.add_argument("--message", default="", help="for the log only")
    ap.add_argument("--vasm",
                    default=os.path.join(os.path.dirname(__file__),
                                         "../external/vasm-src/vasm/vasmm68k_mot"))
    a = ap.parse_args()

    outdir = tempfile.mkdtemp(prefix="scroller_v_")
    assemble(os.path.abspath(a.vasm), a.asm, outdir)
    imgs, land = run_and_measure(a.hatari, a.tos, outdir)
    ok, npix, why, ks, lags = verify(imgs, land, a.speed)
    if a.message:
        print(f"  message       : {a.message!r}")
    print(f"  frame0->k lags : {list(zip(ks, lags))}")
    print(f"  check          : {why}")
    print("RESULT:", "PASS — renders + scrolls smoothly left, no seam" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
