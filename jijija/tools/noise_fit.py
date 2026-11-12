#!/usr/bin/env python3
"""Per-channel noise statistics for raw logs (Phase 2.1 starter).

Usage: python3 tools/noise_fit.py LOGFILE [--organ NAME] [--t0 SEC --t1 SEC]
For each channel of each organ in the selected window prints: mean, std, min/max, number of distinct
codes, estimated sample rate, lag-1 autocorrelation, and an Allan deviation table. Intended for
stationary sessions (S1/S2); do not run it on event sessions and do not touch hold-out data.
"""
import argparse
import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import log_parser as lp  # noqa: E402


def allan_dev(x, fs, taus_s):
    out = []
    for tau in taus_s:
        m = int(round(tau * fs))
        if m < 1 or 2 * m >= len(x):
            continue
        k = len(x) // m
        means = x[:k * m].reshape(k, m).mean(axis=1)
        if len(means) < 3:
            continue
        out.append((m / fs, float(np.sqrt(0.5 * np.mean(np.diff(means) ** 2))), len(means)))
    return out


def analyse(rows, name, ch):
    t = np.array([r.t_us for r in rows], dtype=np.float64) * 1e-6
    x = np.array([r.values[ch] for r in rows], dtype=np.float64)
    fs = (len(t) - 1) / (t[-1] - t[0]) if len(t) > 2 and t[-1] > t[0] else float("nan")
    print(f"\n{name}[{ch}]  n={len(x)}  fs~{fs:.2f} Hz  mean={x.mean():.3f}  std={x.std(ddof=1):.3f}  "
          f"min={x.min():.0f} max={x.max():.0f}  distinct={len(np.unique(x))}")
    xc = x - x.mean()
    if x.std() > 0:
        print(f"   lag-1 autocorr={float(np.dot(xc[:-1], xc[1:]) / np.dot(xc, xc)):.3f}")
    ad = allan_dev(x, fs, [1 / fs * 2 ** i for i in range(0, 20)])
    if ad:
        print("   Allan deviation: " + "  ".join(f"{tau:g}s:{v:.3g}" for tau, v, _ in ad[:12]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("logfile")
    ap.add_argument("--organ")
    ap.add_argument("--t0", type=float)
    ap.add_argument("--t1", type=float)
    a = ap.parse_args()
    _, recs, _ = lp.read_log(a.logfile)
    recs = [r for r in recs if not r.read_error]
    if not recs:
        sys.exit("no usable records")
    t_base = min(r.t_us for r in recs)
    by = {}
    for r in recs:
        ts = (r.t_us - t_base) * 1e-6
        if (a.t0 is not None and ts < a.t0) or (a.t1 is not None and ts > a.t1):
            continue
        by.setdefault(r.organ, []).append(r)
    for organ, rows in sorted(by.items()):
        name = lp.ORGAN_NAMES.get(organ, f"organ{organ}")
        if organ in (14, 15) or (a.organ and a.organ != name):
            continue
        for ch in range(len(rows[0].values)):
            analyse(rows, name, ch)


if __name__ == "__main__":
    main()
