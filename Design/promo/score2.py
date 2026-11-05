"""Original soundtrack for the Sculpt3D making-of video (55.23 s).
20 bars of ambient tech that builds while the model is built, with the final hit on the
"Done" caption at 52.1 s. Pure numpy synthesis, so it's original and royalty-free."""
import numpy as np, wave, sys
SR = 48000
DUR = 30.0
HIT = 24.5
NB = 10
BAR = HIT / NB               # 2.605 s  (~92 BPM)
BEAT = BAR / 4
N = int(SR * DUR)
L = np.zeros(N); R = np.zeros(N)
rng = np.random.default_rng(7)

def hz(m): return 440.0 * 2 ** ((m - 69) / 12)
def add(sig, t0, pan=0.0, gain=1.0):
    i = int(t0 * SR)
    if i >= N: return
    sig = sig[: N - i] * gain
    L[i:i + len(sig)] += sig * np.cos((pan + 1) * np.pi / 4)
    R[i:i + len(sig)] += sig * np.sin((pan + 1) * np.pi / 4)
def lowpass(x, cut):
    a = np.exp(-2 * np.pi * cut / SR); y = np.empty_like(x); s = 0.0
    for k in range(len(x)): s = (1 - a) * x[k] + a * s; y[k] = s
    return y
def lp_fast(x, cut):          # one-pole via cumulative trick on blocks (fast enough)
    a = np.exp(-2 * np.pi * cut / SR)
    from numpy.lib.stride_tricks import sliding_window_view  # noqa
    y = np.empty_like(x); s = 0.0
    xs = (1 - a) * x
    for k in range(len(x)): s = xs[k] + a * s; y[k] = s
    return y

# A minor: Am(add9) | Fmaj7 | Cmaj7 | G6   (each 2 bars) -> 8 bars, repeated
CH = [[57, 60, 64, 71], [53, 57, 60, 64], [48, 55, 59, 64], [55, 59, 62, 64]]
ROOT = [45, 41, 48, 43]
def chord_at(bar): return (bar // 2) % 4

# ---- pad (soft detuned harmonics, slow swell)
def pad(notes, length, bright):
    t = np.arange(int(length * SR)) / SR
    s = np.zeros_like(t)
    for m in notes:
        for det in (-0.07, 0.07):
            f = hz(m) * 2 ** (det / 12)
            for h, amp in ((1, 1), (2, .35 * bright), (3, .15 * bright), (4, .07 * bright)):
                s += amp * np.sin(2 * np.pi * f * h * t + rng.uniform(0, 6.28))
    env = np.minimum(1, t / 0.9) * np.minimum(1, (length - t) / 0.9)
    return s * env / (len(notes) * 4)
for b in range(0, NB + 2, 2):
    bright = 0.4 + 0.6 * min(b, NB) / NB
    p = pad(CH[chord_at(b)], 2 * BAR + 0.9, bright)
    vol = 0.22 if b < 2 else 0.18 if b < NB else 0.14
    add(p, b * BAR, -0.35, vol); add(p[::-1][::-1], b * BAR + 0.012, 0.35, vol)

# ---- arpeggio (16ths), enters bar 2, brighter from bar 12
def pluck(m, length=0.32, bright=1.0):
    t = np.arange(int(length * SR)) / SR; f = hz(m)
    s = np.sin(2*np.pi*f*t) + .3*bright*np.sin(2*np.pi*2*f*t) + .12*bright*np.sin(2*np.pi*3*f*t)
    return s * np.exp(-t * 11) * np.minimum(1, t / 0.003)
pattern = [0, 2, 1, 3, 2, 1, 3, 2]
for b in range(1, NB):
    notes = CH[chord_at(b)]
    for k in range(16):
        n = notes[pattern[k % 8]] + 12 + (12 if (b >= 6 and k % 4 == 3) else 0)
        g = (0.10 if b < 2 else 0.14 if b < 6 else 0.17) * (1.0 if k % 4 == 0 else 0.7)
        if b < 2: g *= (b - 1 + k / 16)          # fade in
        t0 = b * BAR + k * BEAT / 4
        add(pluck(n, bright=0.6 + 0.4 * (b >= 6)), t0, 0.45 if k % 2 else -0.45, g)
        add(pluck(n, bright=0.5), t0 + BEAT * 0.75, -0.6 if k % 2 else 0.6, g * 0.3)   # echo

# ---- bass (8ths), from bar 4
def bass(m, length):
    t = np.arange(int(length * SR)) / SR; f = hz(m - 12)
    s = np.tanh(1.6 * (np.sin(2*np.pi*f*t) + .25*np.sin(2*np.pi*2*f*t)))
    return s * np.exp(-t * 5) * np.minimum(1, t / 0.005)
for b in range(2, NB):
    r = ROOT[chord_at(b)]
    for k in range(8):
        add(bass(r, BEAT / 2 * 0.95), b * BAR + k * BEAT / 2, 0, 0.16 if k % 2 == 0 else 0.11)

# ---- drums
def kick(g=1.0):
    t = np.arange(int(0.45 * SR)) / SR
    f = 48 + 110 * np.exp(-t * 32)
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 7) * g
def hat(length=0.06):
    t = np.arange(int(length * SR)) / SR
    n = rng.standard_normal(len(t)); n = np.diff(n, prepend=0); n = np.diff(n, prepend=0)
    return n * np.exp(-t * 70) * 0.08
for b in range(2, NB):
    for q in range(4):
        add(kick(0.55 if b < 6 else 0.7), b * BAR + q * BEAT, 0, 0.55 if q % 2 == 0 else 0.4)
    if b >= 6:
        for k in range(8):
            add(hat(), b * BAR + k * BEAT / 2 + (BEAT / 4 if k % 2 else 0), 0.3 if k % 2 else -0.3, 0.9 if k % 2 else 0.55)

# ---- riser over the last two bars before the hit
rl = 2 * BAR; t = np.arange(int(rl * SR)) / SR
noise = rng.standard_normal(len(t)); noise = lp_fast(noise, 3500)
riser = noise * (t / rl) ** 2.2 * 0.5
sweep = np.sin(2 * np.pi * np.cumsum(hz(57) * 2 ** (2 * t / rl)) / SR) * (t / rl) ** 3 * 0.12
add(riser + sweep, HIT - rl, -0.2, 0.9); add(riser, HIT - rl + 0.01, 0.2, 0.9)

# ---- final hit at "Done": big kick, noise splash, full A-minor(add9) chord ringing out
add(kick(1.0), HIT, 0, 1.0)
tl = DUR - HIT; t = np.arange(int(tl * SR)) / SR
splash = lp_fast(rng.standard_normal(len(t)), 6000) * np.exp(-t * 3.5) * 0.35
add(splash, HIT, -0.3); add(splash[::-1][::-1], HIT + 0.008, 0.3)
final = np.zeros_like(t)
for m in (45, 57, 64, 69, 71, 76):
    f = hz(m)
    final += (np.sin(2*np.pi*f*t) + .3*np.sin(2*np.pi*2*f*t)) * np.exp(-t * (0.55 if m > 60 else 0.45))
final *= np.minimum(1, t / 0.01) / 6
add(final, HIT, -0.25, 0.55); add(final, HIT + 0.015, 0.25, 0.55)

# ---- simple stereo reverb (FFT convolution with decaying noise)
def reverb(x, seed):
    ir_t = np.arange(int(2.2 * SR)) / SR
    ir = np.random.default_rng(seed).standard_normal(len(ir_t)) * np.exp(-ir_t * 3.0)
    ir = lp_fast(ir, 5000); ir /= np.sqrt((ir ** 2).sum())
    n = len(x) + len(ir); nf = 1 << (n - 1).bit_length()
    y = np.fft.irfft(np.fft.rfft(x, nf) * np.fft.rfft(ir, nf), nf)[: len(x)]
    return y
wetL, wetR = reverb(L, 1), reverb(R, 2)
outL = L + 0.28 * wetL; outR = R + 0.28 * wetR

# fade in/out, normalize
fi = int(0.4 * SR); fo = int(1.6 * SR)
for ch in (outL, outR):
    ch[:fi] *= np.linspace(0, 1, fi); ch[-fo:] *= np.linspace(1, 0, fo) ** 1.5
peak = max(np.abs(outL).max(), np.abs(outR).max())
st = np.stack([outL, outR], 1) / peak * 0.89
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((st * 32767).astype("<i2").tobytes())
print("wrote", sys.argv[1], f"{DUR}s, bar={BAR:.3f}s")
