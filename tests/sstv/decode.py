#!/usr/bin/env python3
"""decode.py: SSTV decoder for the encoder tests (numpy + Pillow only).

    decode.py IN.wav OUT.png [--rate NUM/DEN] [--ref card.ppm] [--json]

Demodulates the FM with an FFT Hilbert transform, finds the VIS header, fits the line period to
the 1200 Hz sync pulses, samples the scans at the nominal segment offsets of the mode the VIS
names (Robot 36, Robot 72, Martin M1) and rebuilds the picture. With --ref it compares the
picture with the reference PPM. Exit status 1 if any check fails; the checks are printed as
PASS/FAIL lines. --rate overrides the WAV header's integer sample rate with an exact fraction."""
import sys, wave, json, argparse
import numpy as np
from PIL import Image

MODES = {   # vis: (name, lines, line_us, segments); a segment is (kind, chan, hz, us)
    8:  ("Robot 36", 240, 150000, {
            0: [("t", 0, 1200, 9000), ("t", 0, 1500, 3000), ("s", "Y", 0, 88000), ("t", 0, 1500, 4500), ("t", 0, 1900, 1500), ("s", "RY", 0, 44000)],
            1: [("t", 0, 1200, 9000), ("t", 0, 1500, 3000), ("s", "Y", 0, 88000), ("t", 0, 2300, 4500), ("t", 0, 1900, 1500), ("s", "BY", 0, 44000)]}),
    12: ("Robot 72", 240, 300000, {
            0: [("t", 0, 1200, 9000), ("t", 0, 1500, 3000), ("s", "Y", 0, 138000), ("t", 0, 1500, 4500), ("t", 0, 1900, 1500), ("s", "RY", 0, 69000),
                ("t", 0, 2300, 4500), ("t", 0, 1500, 1500), ("s", "BY", 0, 69000)]}),
    44: ("Martin M1", 256, 446446, {
            0: [("t", 0, 1200, 4862), ("t", 0, 1500, 572), ("s", "G", 0, 146432), ("t", 0, 1500, 572), ("s", "B", 0, 146432), ("t", 0, 1500, 572),
                ("s", "R", 0, 146432), ("t", 0, 1500, 572)]}),
}
VIS_US = 300000 + 10000 + 300000 + 30000 + 7 * 30000 + 30000 + 30000      # leader .. stop bit = 910 ms

results = []
def check(name, ok, detail=""):
    results.append((name, bool(ok), detail))
    print(f"{'PASS' if ok else 'FAIL'}: {name}{(' (' + detail + ')') if detail else ''}")

def read_wav(path):
    w = wave.open(path, "rb")
    n, sw, ch, fs = w.getnframes(), w.getsampwidth(), w.getnchannels(), w.getframerate()
    raw = w.readframes(n); w.close()
    if sw == 1: x = np.frombuffer(raw, np.uint8).astype(np.float64) - 128
    elif sw == 2: x = np.frombuffer(raw, "<i2").astype(np.float64)
    else: raise SystemExit("unsupported sample width")
    if ch > 1: x = x.reshape(-1, ch).mean(axis=1)
    return x, fs

def inst_freq(x, fs):
    n = len(x); N = 1 << (n - 1).bit_length()
    X = np.fft.rfft(x, N)
    # analytic signal: zero the negative frequencies (rfft already has only the positive half)
    H = np.ones(len(X)); H[1:-1] = 2.0
    z = np.fft.ifft(np.concatenate([X * H, np.zeros(N - len(X), complex)]))[:n]
    ph = np.unwrap(np.angle(z))
    f = np.diff(ph) * fs / (2 * np.pi)
    f = np.append(f, f[-1])
    k = max(3, int(round(fs * 0.0015)) | 1)                   # 1.5 ms box smoothing
    f = np.convolve(f, np.ones(k) / k, mode="same")
    return f

def runs(cls):
    """(class, start, length) runs of a class array."""
    edges = np.flatnonzero(np.diff(cls)) + 1
    starts = np.concatenate([[0], edges]); ends = np.concatenate([edges, [len(cls)]])
    return [(cls[s], s, e - s) for s, e in zip(starts, ends)]

def find_vis(f, fs):
    """Return (index of the VIS start bit's leading edge, code) or raise."""
    ms = fs / 1000.0
    cls = np.full(len(f), -1, int)
    for i, (lo, hi) in enumerate([(1050, 1150), (1150, 1250), (1250, 1350), (1800, 2000)]):   # 1100 1200 1300 1900
        cls[(f >= lo) & (f < hi)] = i
    r = [x for x in runs(cls) if x[2] >= 2 * ms]          # drop the 1 ms ramps between tones
    for i in range(len(r) - 3):
        a, b, c, d = r[i], r[i + 1], r[i + 2], r[i + 3]
        if a[0] == 3 and a[2] > 250 * ms and b[0] == 1 and 5 * ms < b[2] < 15 * ms and c[0] == 3 and c[2] > 250 * ms \
           and d[0] == 1 and 20 * ms < d[2] < 40 * ms:
            start = d[1]
            bits = []
            for k in range(8):
                centre = start + int(round((30 * (k + 1) + 15) * ms))
                bits.append(1 if f[centre] < 1200 else 0)
            code = sum(b << k for k, b in enumerate(bits[:7]))
            parity_ok = (sum(bits[:7]) & 1) == bits[7]
            stop = f[start + int(round((30 * 9 + 15) * ms))]
            return start, code, parity_ok, abs(stop - 1200) < 60, (a[1], a[2])
    raise SystemExit("no VIS header found")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav"); ap.add_argument("png")
    ap.add_argument("--rate", help="exact sample rate NUM/DEN (overrides the header)")
    ap.add_argument("--ref", help="reference PPM (320x240) to compare with")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    x, fs_hdr = read_wav(a.wav)
    fs = fs_hdr
    if a.rate:
        num, den = a.rate.split("/"); fs = float(num) / float(den)
    f = inst_freq(x, fs)
    start, code, parity_ok, stop_ok, leader = find_vis(f, fs)
    check("VIS parity", parity_ok); check("VIS stop bit", stop_ok)
    check("VIS code known", code in MODES, f"code {code}")
    if code not in MODES: finish(a); return
    name, lines, line_us, tables = MODES[code]
    print(f"mode {name} (VIS {code}), leader {leader[1] / fs * 1000:.1f} ms of 1900 Hz, sample rate {fs:.3f} Hz")
    spus = fs / 1e6
    t_lines = start + 300 * fs / 1000.0                      # start bit + 8 bits + stop bit = 300 ms
    # sync pulse leading edges: the frequency drops to 1200 from the previous scan/separator
    nominal = t_lines + np.arange(lines) * line_us * spus
    meas = []
    win = int(3 * fs / 1000)
    for k in range(1, lines):
        n0 = int(round(nominal[k]))
        seg = f[n0 - win:n0 + win]
        below = np.flatnonzero(seg < 1350)
        if len(below) == 0: continue
        meas.append((k, n0 - win + below[0]))
    ks = np.array([m[0] for m in meas], float); ts = np.array([m[1] for m in meas], float)
    slope, icpt = np.polyfit(ks, ts, 1)
    period_us = slope / spus
    ppm = (period_us - line_us) / line_us * 1e6
    check("sync pulses found on every line", len(meas) == lines - 1, f"{len(meas)} of {lines - 1}")
    check("line period within 20 ppm", abs(ppm) < 20, f"{period_us:.3f} us, {ppm:+.1f} ppm")
    # total: from the VIS leader's start to the end of the last line, against the nominal
    end = icpt + slope * lines                              # extrapolated start of line `lines`
    dur_us = (end - leader[0]) / spus
    nominal_us = leader[1] / spus + (start - (leader[0] + leader[1])) / spus + 300000 + lines * line_us
    # the leader run as measured includes its own length; compare the VIS-start -> end stretch instead
    dur_us = (end - start) / spus; nominal_us = 300000 + lines * line_us
    check("duration within 1 ms", abs(dur_us - nominal_us) < 1000, f"{dur_us / 1000:.3f} ms vs {nominal_us / 1000:.3f} ms")
    # first line start relative to the VIS: the fit's intercept should land on t_lines
    check("first line within 1 ms of the VIS end", abs(icpt - t_lines) / spus < 1000, f"{(icpt - t_lines) / spus / 1000:+.3f} ms")
    # sample the scans
    W = 320
    chans = {}
    for ch in ("Y", "RY", "BY", "R", "G", "B"): chans[ch] = np.zeros((lines, W))
    for k in range(lines):
        t0 = icpt + slope * k
        table = tables[k & 1] if len(tables) > 1 else tables[0]
        off = 0.0
        for kind, ch, hz, us in table:
            if kind == "s":
                px = t0 + (off + (np.arange(W) + 0.5) * us / W) * spus
                v = (f[np.clip(np.round(px).astype(int), 0, len(f) - 1)] - 1500) * 255 / 800
                chans[ch][k] = np.clip(v, 0, 255)
            off += us
    if name.startswith("Robot"):
        Y = chans["Y"]
        if code == 8:                                       # chroma per line pair
            RY = chans["RY"].copy(); BY = chans["BY"].copy()
            for k in range(0, lines, 2):
                RY[k + 1] = RY[k]; BY[k] = BY[k + 1]
        else:
            RY, BY = chans["RY"], chans["BY"]
        Yc = 1.164 * (Y - 16); Cr = RY - 128; Cb = BY - 128
        R = Yc + 1.596 * Cr; G = Yc - 0.813 * Cr - 0.392 * Cb; B = Yc + 2.017 * Cb
    else:
        R, G, B = chans["R"], chans["G"], chans["B"]
    img = np.clip(np.stack([R, G, B], -1), 0, 255).astype(np.uint8)
    Image.fromarray(img).save(a.png)
    print(f"picture {W}x{lines} written to {a.png}")
    if a.ref:
        ref = np.asarray(Image.open(a.ref).convert("RGB")).astype(float)
        h = min(ref.shape[0], lines)
        dec = img[:h].astype(float)
        yd = 0.299 * dec[..., 0] + 0.587 * dec[..., 1] + 0.114 * dec[..., 2]
        yr = 0.299 * ref[:h, :, 0] + 0.587 * ref[:h, :, 1] + 0.114 * ref[:h, :, 2]
        # ignore a 2-pixel border of each bar/row where the filters smear transitions
        err = np.abs(yd - yr)
        check("mean |dY| < 24/255", err.mean() < 24, f"{err.mean():.2f}")
        worst = 0
        for b in range(8):                                   # the bars: rows 0..159, 40 px wide
            x0 = b * 40 + 6; x1 = (b + 1) * 40 - 6
            d = np.abs(dec[8:152, x0:x1].mean(axis=(0, 1)) - ref[8:152, x0:x1].mean(axis=(0, 1)))
            worst = max(worst, d.max())
        check("colour bars within 40/255 per channel", worst < 40, f"worst {worst:.1f}")
        if lines > 240:
            pad = img[244:255, :316]                        # the last pixel of the last line meets the trailer
            check("padding lines black", pad.max() < 40, f"max {pad.max()}")
    finish(a)

def finish(a):
    if a.json: print(json.dumps([{"check": n, "ok": ok, "detail": d} for n, ok, d in results]))
    sys.exit(0 if all(ok for _, ok, _ in results) else 1)

main()
