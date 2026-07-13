#!/usr/bin/env python3
"""Talos Phase 4 intra-line pilot: a uniform vertical raster split.

Proves cycle-*within-line* placement (the Spectrum-512 direction, harder than the
per-line raster bars). Each scanline writes colour A at line start, burns a tuned
delay, writes colour B, then fills the rest of the line — so the A->B boundary is
a vertical line whose column tracks the delay. The whole line stays ~512 cyc
(ST PAL) so the split doesn't slant.

Usage:
  intraline_split.py --hatari <bin> --tos <rom> [--left N] [--k K] [--delay D]
  intraline_split.py ... --sweep         # map left-dbf -> split column (calibration)
"""
import argparse, os, signal, socket, subprocess, sys, tempfile, time
import numpy as np
from PIL import Image

RDB_PORT = 56001
NLINES = 180
SHOT_VBL = 3000
COLA, COLB = 0x007, 0x700     # left = blue, right = red

# Empirical HBL-sync calibration (ST PAL, this fork build): the B write lands at
# surface column ~= COL0 + PXPERDBF * left. Measured by --sweep; bench-validated
# per C-007 rather than trusted from instruction-cycle tables.
COL0, PXPERDBF = 162, 24
def left_for_col(col):
    return max(0, min(29, round((col - COL0) / PXPERDBF)))


def st_rgb(v):
    def gun(n):
        i4 = ((n & 7) << 1) | ((n & 8) >> 3)
        return i4 | (i4 << 4)
    return (gun((v >> 8) & 0xf), gun((v >> 4) & 0xf), gun(v & 0xf))


def codegen(cola, colb, left, k=0, delay_c=0, nlines=NLINES):
    """HBL-synced split: every scanline the HBL interrupt (level 2, low jitter
    because the CPU is STOPped) writes colour A, burns `left` dbf iterations, then
    writes colour B. Re-synced each line, so the A->B boundary is a vertical line
    whose column is set by `left` — no cross-line drift to tune out."""
    return f"""; split.s — Talos intra-line vertical raster split (HBL-synced, generated).
IERA    equ $fffffa07
IERB    equ $fffffa09
PAL0    equ $ffff8240
    text
start:
    clr.l   -(sp)
    move.w  #$20,-(sp)
    trap    #1
    addq.l  #6,sp
    move.b  #0,IERA              ; MFP off -> only HBL/VBL fire
    move.b  #0,IERB
    move.l  $44e.w,a0            ; clear screen to palette 0 (whole line = bg)
    move.w  #(32000/4)-1,d0
    moveq   #0,d1
.clr:
    move.l  d1,(a0)+
    dbf     d0,.clr
    move.l  #hbl,$68.w           ; HBL autovector -> the per-line split
    move.l  #vbl,$70.w           ; VBL autovector -> nothing (just wake/return)
main:
    stop    #$2100               ; IPL1: level-2 HBL wakes us with fixed latency
    bra.s   main

hbl:
    move.w  #${cola:03x},PAL0        ; left colour (from HBL, persists into the line)
    move.w  #{left},d0
.l: dbf     d0,.l                    ; delay -> split column
    move.w  #${colb:03x},PAL0        ; right colour at the split
    rte

vbl:
    rte
"""


RAINBOW = [0x700, 0x070, 0x007, 0x770, 0x707, 0x077, 0x777, 0x330]


def codegen_multi(colours, left, nlines=NLINES):
    """Spectrum-512-lite: the HBL handler writes each colour in turn -> N vertical
    bands per scanline, HBL-synced so all stable. left>0 inserts a dbf delay between
    writes (few wide bands); left<=0 packs the writes back-to-back (many narrow
    bands, as real Spectrum 512 does — ~44+ changes/line for 512 on-screen colours).
    The whole handler must fit inside one scanline or the HBL sync breaks."""
    body = ""
    for i, c in enumerate(colours):
        body += f"    move.w  #${c:03x},PAL0\n"
        if i < len(colours) - 1 and left > 0:
            body += f"    move.w  #{left},d0\n.l{i}: dbf     d0,.l{i}\n"
    return f"""; multisplit.s — Talos intra-line multi-split (HBL-synced, generated).
IERA    equ $fffffa07
IERB    equ $fffffa09
PAL0    equ $ffff8240
    text
start:
    clr.l   -(sp)
    move.w  #$20,-(sp)
    trap    #1
    addq.l  #6,sp
    move.b  #0,IERA
    move.b  #0,IERB
    move.l  $44e.w,a0
    move.w  #(32000/4)-1,d0
    moveq   #0,d1
.clr:
    move.l  d1,(a0)+
    dbf     d0,.clr
    move.l  #hbl,$68.w
    move.l  #vbl,$70.w
main:
    stop    #$2100
    bra.s   main
hbl:
{body}    rte
vbl:
    rte
"""


def measure_bands(img, colours):
    """Count the colour bands per line and how vertical they are. The palette
    cycles, so verticality is measured *positionally*: for rows with the modal
    number of colour changes, the stddev of the k-th change's column. Returns
    (modal_band_count, median_positional_stddev)."""
    from collections import Counter
    pal = [np.array(st_rgb(c)) for c in dict.fromkeys(colours)]   # distinct colours

    def transitions(y):
        row = img[y].astype(int)
        cols, prev = [], None
        for x in range(img.shape[1]):
            d = [int(np.abs(row[x] - p).sum()) for p in pal]
            m = min(range(len(pal)), key=lambda i: d[i])
            v = m if d[m] < 120 else -1
            if v < 0:
                continue        # border / unclassified
            if prev is not None and v != prev:
                cols.append(x)
            prev = v
        return cols

    rows = [t for t in (transitions(y) for y in range(0, img.shape[0], 3)) if t]
    if not rows:
        return 0, None
    modal = Counter(len(r) for r in rows).most_common(1)[0][0]
    kept = [r for r in rows if len(r) == modal]
    if len(kept) < 10:
        return modal, None
    arr = np.array(kept)
    return modal, float(np.median(arr.std(axis=0)))


def assemble(asm, vasm, outdir):
    os.makedirs(os.path.join(outdir, "AUTO"), exist_ok=True)
    src = os.path.join(outdir, "split.s")
    open(src, "w").write(asm)
    r = subprocess.run([vasm, "-Ftos", "-o", os.path.join(outdir, "AUTO", "SPLIT.PRG"), src],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("vasm:\n" + r.stdout + r.stderr)


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
            if st and len(st) > 1 and st[1] == b"0": return
            time.sleep(0.01)
    def to_vbl(s, v):
        s.cmd("break"); s.wait(); s.cmd(f"bp VBL = {v} :once"); s.cmd("run"); s.wait()


def run_shot(hatari, tos, effdir, png):
    cfg = os.path.join(tempfile.gettempdir(), "split.cfg"); open(cfg, "w").close()
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
        r.cmd("break"); r.drain(); r.wait()
        r.to_vbl(SHOT_VBL)
        r.cmd(f"console screenshot {png}"); time.sleep(0.6)
    finally:
        try: os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except ProcessLookupError: pass
    return np.asarray(Image.open(png).convert("RGB"))


def measure_split(img, cola, colb):
    """For rows that contain both colours, find the A->B transition column.
    Returns (median_col, vertical_stddev, n_rows_with_split)."""
    a = np.array(st_rgb(cola)); b = np.array(st_rgb(colb))
    cols = []
    for y in range(img.shape[0]):
        row = img[y].astype(int)
        is_a = np.all(np.abs(row - a) <= 28, axis=1)
        is_b = np.all(np.abs(row - b) <= 28, axis=1)
        if is_a.any() and is_b.any():
            # first B pixel that comes after some A
            first_a = np.argmax(is_a)
            b_after = np.where(is_b & (np.arange(len(is_b)) > first_a))[0]
            if len(b_after):
                cols.append(int(b_after[0]))
    if not cols:
        return None, None, 0
    return int(np.median(cols)), float(np.std(cols)), len(cols)


def build_and_measure(a, left, k):
    outdir = tempfile.mkdtemp(prefix="split_")
    assemble(codegen(COLA, COLB, left, k, a.delay), os.path.abspath(a.vasm), outdir)
    png = os.path.join(tempfile.gettempdir(), f"split_{left}.png")
    img = run_shot(a.hatari, a.tos, outdir, png)
    return measure_split(img, COLA, COLB), png


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hatari", required=True)
    ap.add_argument("--tos", required=True)
    ap.add_argument("--vasm", default=os.path.join(os.path.dirname(__file__),
                    "../external/vasm-src/vasm/vasmm68k_mot"))
    ap.add_argument("--left", type=int, default=10)
    ap.add_argument("--col", type=int, help="target split column (uses the calibration)")
    ap.add_argument("--k", type=int, default=45)
    ap.add_argument("--delay", type=int, default=900)
    ap.add_argument("--sweep", action="store_true")
    ap.add_argument("--multi", action="store_true", help="Spectrum-lite: N bands/line")
    ap.add_argument("--gap", type=int, default=0, help="dbf delay between bands (0 = packed)")
    ap.add_argument("--nbands", type=int, default=20, help="number of colour writes/line")
    ap.add_argument("--colours", help="comma-separated RGB list (overrides --nbands)")
    a = ap.parse_args()

    if a.multi:
        if a.colours:
            colours = [int(x, 16) for x in a.colours.split(",") if x.strip()]
        else:
            colours = [RAINBOW[i % len(RAINBOW)] for i in range(a.nbands)]
        outdir = tempfile.mkdtemp(prefix="multi_")
        assemble(codegen_multi(colours, a.gap), os.path.abspath(a.vasm), outdir)
        png = os.path.join(tempfile.gettempdir(), "multisplit.png")
        img = run_shot(a.hatari, a.tos, outdir, png)
        nb, vstd = measure_bands(img, colours)
        print(f"  {a.nbands} writes, gap={a.gap}: centre-row bands seen={nb}  vstd={vstd}")
        print(f"  screenshot: {png}")
        ok = nb >= 2 and vstd is not None and vstd < 12
        print("RESULT:", "PASS — multiple stable vertical bands per line" if ok else "FAIL")
        sys.exit(0 if ok else 1)

    if a.sweep:
        print(f"  sweep (k={a.k}, delay={a.delay}): left-dbf -> split column / verticality")
        for left in range(0, a.k + 1, max(1, a.k // 9)):
            (col, sd, n), _ = build_and_measure(a, left, a.k)
            print(f"    left={left:3}  split_col={col}  vstd={sd if sd is None else round(sd,1)}  rows={n}")
    else:
        left = left_for_col(a.col) if a.col is not None else a.left
        (col, sd, n), png = build_and_measure(a, left, a.k)
        target = f", target≈{a.col}" if a.col is not None else ""
        print(f"  left={left}{target}: split_col={col} vstd={sd} rows={n}")
        print(f"  screenshot: {png}")
        near = a.col is None or (col is not None and abs(col - a.col) <= 30)
        ok = col is not None and n > 40 and sd is not None and sd < 10 and near
        print("RESULT:", "PASS — clean vertical split at the target column" if ok
              else "FAIL / not vertical or off-target")
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
