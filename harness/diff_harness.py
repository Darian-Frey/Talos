#!/usr/bin/env python3
"""Talos per-scanline validation harness (F-213, D-009).

Runs an effect through Hatari to a fixed, deterministic sync point (an absolute
VBL count) and screenshots the taken framebuffer, then diffs frames per scanline.

At the current B1 stage Talos adds no emulation-affecting fork patches, so the
harness verifies the two properties that matter:

  * determinism  — two identical driven runs to VBL T produce an identical frame;
  * non-perturbation — a run that performs a register-write CAPTURE before VBL T
    produces the same frame as one that does not. If Talos's break/step/capture
    driving changed the emulation, this frame would diverge.

Any divergence therefore implicates Talos's driving/config, not the emulation —
the small search space D-009 is built around.

Usage:
  diff_harness.py --hatari <bin> --tos <rom> --effect <gemdos-dir> [--reg ffff8240]

Exit code 0 = all checks passed (frames identical), 1 = divergence, 2 = error.
"""

import argparse
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time

import numpy as np
from PIL import Image

RDB_PORT = 56001
CAP_VBL = 10000      # capture happens here (absolute VBL; > any connect-time VBL)
SHOT_VBL = 12000     # screenshot here; both reference and driven runs sync to this
CAP_COUNT = 48       # writes to capture in the driven run


# --------------------------------------------------------------------------- IO
class Rdb:
    """Minimal remote-debug client: 0x1-separated tokens, 0x0-terminated."""

    def __init__(self, host="127.0.0.1", port=RDB_PORT, timeout=30):
        self.s = socket.create_connection((host, port), timeout=5)
        self.s.settimeout(timeout)
        self.buf = bytearray()

    def _msg(self):
        while True:
            i = self.buf.find(0)
            if i >= 0:
                m = bytes(self.buf[:i])
                del self.buf[: i + 1]
                return m.split(b"\x01")
            chunk = self.s.recv(4096)
            if not chunk:
                return None
            self.buf += chunk

    def cmd(self, line):
        self.s.sendall(line.encode() + b"\0")
        while True:
            m = self._msg()
            if m is None:
                return None
            if m and m[0] in (b"OK", b"NG"):   # skip '!' notifications
                return m

    def drain_greeting(self):
        self.s.settimeout(0.4)
        try:
            while True:
                self.s.recv(4096)
        except socket.timeout:
            pass
        self.s.settimeout(30)

    def regs(self):
        m = self.cmd("regs")
        flat = [t for t in m if t] if m else []
        kv = {}
        if flat and flat[0] == b"OK":
            flat = flat[1:]
        for j in range(0, len(flat) - 1, 2):
            kv[flat[j].decode("latin1")] = int(flat[j + 1], 16)
        return kv

    def wait_stopped(self, tries=1500):
        for _ in range(tries):
            st = self.cmd("status")
            if st and len(st) > 1 and st[1] == b"0":
                return True
            time.sleep(0.01)
        return False

    def run_to_vbl(self, target):
        self.cmd("break")
        self.wait_stopped()
        if self.cmd(f"bp VBL = {target} :once")[0] != b"OK":
            raise RuntimeError(f"bp VBL={target} rejected")
        self.cmd("run")
        if not self.wait_stopped():
            raise RuntimeError(f"never reached VBL {target}")

    def capture(self, addr, count):
        self.cmd("break")
        self.wait_stopped()
        got = 0
        while got < count:
            a = f"{addr:x}"
            if self.cmd(f"bp ( ${a} ).w ! ( ${a} ).w :once")[0] != b"OK":
                raise RuntimeError("capture bp rejected")
            self.cmd("run")
            if not self.wait_stopped():
                raise RuntimeError("capture: no write within timeout")
            got += 1

    def screenshot(self, path):
        self.cmd(f"console screenshot {path}")
        time.sleep(0.4)


# ------------------------------------------------------------------------- runs
def launch_hatari(args):
    cfg = os.path.join(tempfile.gettempdir(), "talos_harness_empty.cfg")
    open(cfg, "w").close()
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    return subprocess.Popen(
        [args.hatari, "--configfile", cfg, "--tos", args.tos, "--machine", "st",
         "--sound", "off", "--statusbar", "off", "--fast-forward", "on",
         "-d", args.effect],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        preexec_fn=os.setsid)


def connect_retry(tries=80, delay=0.15):
    """Connect the real client with retries (the RDB server accepts once, so we
    must not throwaway-connect first to probe readiness)."""
    last = None
    for _ in range(tries):
        try:
            return Rdb()
        except OSError as e:
            last = e
            time.sleep(delay)
    raise RuntimeError(f"could not connect to Hatari: {last}")


def kill(proc):
    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
    except ProcessLookupError:
        pass


def run(args, do_capture, tag):
    """One run: sync to CAP_VBL, optionally capture, sync to SHOT_VBL, screenshot."""
    proc = launch_hatari(args)
    png = os.path.join(tempfile.gettempdir(), f"talos_harness_{tag}.png")
    try:
        rdb = connect_retry()
        rdb.cmd("break")            # freeze the racing (fast-forward) VBL early
        rdb.drain_greeting()
        rdb.wait_stopped()
        v0 = rdb.regs().get("VBL", 0)
        if v0 >= CAP_VBL:
            raise RuntimeError(f"connected too late (VBL {v0} >= {CAP_VBL})")
        rdb.run_to_vbl(CAP_VBL)                 # symmetric sync point
        if do_capture:
            rdb.capture(int(args.reg, 16), CAP_COUNT)
        rdb.run_to_vbl(SHOT_VBL)
        rdb.screenshot(png)
    finally:
        kill(proc)
    img = np.asarray(Image.open(png).convert("RGB"))
    return img


# ------------------------------------------------------------------------- diff
def diff(a, b):
    if a.shape != b.shape:
        return None, f"shape mismatch {a.shape} vs {b.shape}"
    row_differs = np.any(a != b, axis=(1, 2))
    rows = np.nonzero(row_differs)[0]
    max_delta = int(np.abs(a.astype(int) - b.astype(int)).max()) if a.size else 0
    return rows, max_delta


def report(name, rows, max_delta, height):
    if rows is None:
        print(f"  {name}: ERROR — {max_delta}")
        return False
    if len(rows) == 0:
        print(f"  {name}: PASS — all {height} scanlines identical")
        return True
    preview = ", ".join(str(int(r)) for r in rows[:12])
    more = "" if len(rows) <= 12 else f", … (+{len(rows) - 12})"
    print(f"  {name}: FAIL — {len(rows)}/{height} scanlines differ "
          f"(max Δ {max_delta}); rows: {preview}{more}")
    return False


def main():
    ap = argparse.ArgumentParser(description="Talos per-scanline validation harness")
    ap.add_argument("--hatari", required=True)
    ap.add_argument("--tos", required=True)
    ap.add_argument("--effect", required=True, help="GEMDOS dir with AUTO/<effect>.PRG")
    ap.add_argument("--reg", default="ffff8240", help="register to capture (hex)")
    args = ap.parse_args()

    try:
        print(f"Talos validation harness — effect {args.effect}, "
              f"sync VBL {SHOT_VBL}, capture VBL {CAP_VBL} (reg ${args.reg})")
        print("running reference #1 …");  ref1 = run(args, False, "ref1")
        print("running reference #2 …");  ref2 = run(args, False, "ref2")
        print(f"running driven (capture {CAP_COUNT} writes) …")
        drv = run(args, True, "driven")
    except Exception as e:  # noqa: BLE001
        print(f"harness error: {e}", file=sys.stderr)
        return 2

    h = ref1.shape[0]
    print(f"\nframes {ref1.shape[1]}x{h}; per-scanline diff:")
    ok_det = report("determinism (ref#1 vs ref#2)", *diff(ref1, ref2), h)
    ok_pert = report("non-perturbation (ref#1 vs driven)", *diff(ref1, drv), h)

    print()
    if ok_det and ok_pert:
        print("HARNESS PASS")
        return 0
    print("HARNESS FAIL")
    return 1


if __name__ == "__main__":
    sys.exit(main())
