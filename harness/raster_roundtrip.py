#!/usr/bin/env python3
"""Talos Phase 4 pilot (F-211/F-212): raster-bar prototype -> asm -> verify.

Proves the prototype->export->verify loop on the tractable case:
  1. codegen a cycle-exact (per-line) raster-bar 68k stub from a bar list,
  2. assemble it with vasm into a GEMDOS AUTO .PRG,
  3. run it in Hatari and screenshot the taken framebuffer,
  4. verify the authored bar colours appear as horizontal bands, in order.

Because the stub *is* the export artefact, running it in stock Hatari reproduces
the effect by construction; this tool is the automated "verify on Hatari".

Usage:
  raster_roundtrip.py --hatari <bin> --tos <rom> [--bar LINE:RGB ...] [--shot out.png]
  (RGB is ST-style 3-nibble hex, e.g. 700=red, 070=green, 007=blue.)
"""
import argparse, os, signal, socket, subprocess, sys, tempfile, time
import numpy as np
from PIL import Image

RDB_PORT = 56001
VISIBLE_LINES = 200          # low-res visible scanlines to cover with bars
CYC_PER_LINE = 512           # ST PAL low-res (C-007); NTSC would be 508
SHOT_VBL = 3000              # screenshot well after boot, effect running


# ---------------------------------------------------------------- ST colour
def st_rgb(v):
    """ST $0rgb (each gun 0-7) -> 8-bit RGB, matching client Palette decode."""
    def gun(n):
        i4 = ((n & 7) << 1) | ((n & 8) >> 3)
        return i4 | (i4 << 4)
    return (gun((v >> 8) & 0xf), gun((v >> 4) & 0xf), gun(v & 0xf))


# ------------------------------------------------------------------- codegen
def codegen(bars, pad, delay_c, total=VISIBLE_LINES):
    """bars: sorted list of (start_line, colour). Emit a per-line colour table
    (piecewise constant) + a VBL-synced, cycle-counted raster-bar stub."""
    colours = []
    for line in range(total):
        c = 0x000
        for start, col in bars:
            if line >= start:
                c = col
        colours.append(c)
    tab = "\n".join(
        "\tdc.w\t" + ",".join(f"${colours[i+j]:03x}" for j in range(min(8, total - i)))
        for i in range(0, total, 8))
    return f"""; raster.s — Talos Phase 4 generated raster bars (F-212). Do not hand-edit.
IERA    equ $fffffa07
IERB    equ $fffffa09
PAL0    equ $ffff8240
    text
start:
    clr.l   -(sp)
    move.w  #$20,-(sp)
    trap    #1
    addq.l  #6,sp
    move.b  #0,IERA              ; MFP off -> Timer-C can't jitter the VBL sync
    move.b  #0,IERB
    move.l  $44e.w,a0            ; clear the screen to palette 0 (whole screen = bg)
    move.w  #(32000/4)-1,d0
    moveq   #0,d1
.clr:
    move.l  d1,(a0)+
    dbf     d0,.clr
    move.l  #vbl,$70.w           ; VBL handler runs the cycle-counted bar loop
main:
    stop    #$2300               ; wait for VBL (jitter-free with MFP off)
    bra.s   main

vbl:
    move.w  #$2700,sr            ; mask: the timing below must be exact
    movem.l d0-d2/a1,-(sp)
    move.w  #{delay_c},d0        ; delay: VBL -> first visible bar line
.d: dbf     d0,.d
    lea     coltab(pc),a1
    move.w  #{total - 1},d2
.line:
    move.w  (a1)+,PAL0           ; this scanline's colour
    move.w  #{pad},d1            ; pad the rest of the scanline (=> {CYC_PER_LINE} cyc/line)
.p: dbf     d1,.p
    dbf     d2,.line
    movem.l (sp)+,d0-d2/a1
    rte

    even
coltab:
{tab}
"""


# ------------------------------------------------------------------ assemble
def assemble(asm, vasm, outdir):
    src = os.path.join(outdir, "raster.s")
    auto = os.path.join(outdir, "AUTO")
    os.makedirs(auto, exist_ok=True)
    with open(src, "w") as f:
        f.write(asm)
    r = subprocess.run([vasm, "-Ftos", "-o", os.path.join(auto, "RASTER.PRG"), src],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("vasm failed:\n" + r.stdout + r.stderr)
    return outdir


# ----------------------------------------------------------------------- run
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
    def wait_stopped(s, t=4000):
        for _ in range(t):
            st = s.cmd("status")
            if st and len(st) > 1 and st[1] == b"0": return True
            time.sleep(0.01)
    def to_vbl(s, v):
        s.cmd("break"); s.wait_stopped()
        s.cmd(f"bp VBL = {v} :once"); s.cmd("run"); s.wait_stopped()


def run_and_shot(hatari, tos, effdir, png):
    cfg = os.path.join(tempfile.gettempdir(), "raster_rt.cfg"); open(cfg, "w").close()
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    p = subprocess.Popen(
        [hatari, "--configfile", cfg, "--tos", tos, "--machine", "st",
         "--sound", "off", "--statusbar", "off", "--fast-forward", "on", "-d", effdir],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, preexec_fn=os.setsid)
    try:
        r = None
        for _ in range(150):
            try: r = Rdb(); break
            except OSError: time.sleep(0.15)
        r.cmd("break"); r.drain(); r.wait_stopped()
        r.to_vbl(SHOT_VBL)
        r.cmd(f"console screenshot {png}"); time.sleep(0.6)
    finally:
        try: os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except ProcessLookupError: pass
    return np.asarray(Image.open(png).convert("RGB"))


# -------------------------------------------------------------------- verify
def bands_in_column(col, min_run=3):
    """Return the ordered list of distinct colours (runs >= min_run px) down a
    column, as a list of (r,g,b)."""
    out = []
    i = 0
    while i < len(col):
        j = i
        while j < len(col) and np.all(np.abs(col[j].astype(int) - col[i]) <= 24):
            j += 1
        if j - i >= min_run:
            out.append(tuple(int(x) for x in col[i]))
        i = j
    return out


def verify(img, bars):
    """Check the authored bar colours appear as horizontal bands, in order,
    somewhere down the centre column."""
    want = [st_rgb(c) for _, c in bars]
    col = img[:, img.shape[1] // 2, :]
    seen = bands_in_column(col)

    def close(a, b): return all(abs(x - y) <= 28 for x, y in zip(a, b))

    # find the authored colour sequence as a subsequence of the seen bands
    wi = 0
    for band in seen:
        if wi < len(want) and close(band, want[wi]):
            wi += 1
    ok = wi == len(want)
    return ok, want, seen


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hatari", required=True)
    ap.add_argument("--tos", required=True)
    ap.add_argument("--vasm",
                    default=os.path.join(os.path.dirname(__file__),
                                         "../external/vasm-src/vasm/vasmm68k_mot"))
    ap.add_argument("--bar", action="append", default=[],
                    help="LINE:RGB (repeatable), e.g. --bar 20:700 --bar 90:070")
    ap.add_argument("--pad", type=int, default=36, help="per-line dbf pad (timing tune)")
    ap.add_argument("--delay", type=int, default=900, help="VBL->first-line delay (tune)")
    ap.add_argument("--shot", default=os.path.join(tempfile.gettempdir(), "raster_rt.png"))
    a = ap.parse_args()

    if a.bar:
        bars = sorted((int(x.split(":")[0]), int(x.split(":")[1], 16)) for x in a.bar)
    else:  # default rainbow: 7 bands
        cols = [0x700, 0x070, 0x007, 0x770, 0x707, 0x077, 0x777]
        bars = [(i * (VISIBLE_LINES // len(cols)), c) for i, c in enumerate(cols)]

    asm = codegen(bars, a.pad, a.delay)
    outdir = tempfile.mkdtemp(prefix="raster_rt_")
    assemble(asm, os.path.abspath(a.vasm), outdir)
    img = run_and_shot(a.hatari, a.tos, outdir, a.shot)
    ok, want, seen = verify(img, bars)
    print(f"  authored bars : {['%02x%02x%02x' % w for w in want]}")
    print(f"  bands seen    : {['%02x%02x%02x' % s for s in seen]}")
    print(f"  screenshot    : {a.shot}")
    print("RESULT:", "PASS — authored bars reproduced in order" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
