"""Analyse a 12+12-bit IQ LA dump: power envelope (bursts) + averaged spectrum (ASCII)."""
import sys, numpy as np
fn = sys.argv[1]; fs = float(sys.argv[2]) if len(sys.argv) > 2 else 40e6
swap = len(sys.argv) > 3 and sys.argv[3] == "swap"
lo = np.fromfile(fn, dtype="<u4").reshape(-1, 2)[:, 0].astype(np.int64)
def sx(v, b):
    v = v & ((1 << b) - 1); return np.where(v >= (1 << (b - 1)), v - (1 << b), v)
a, b = sx(lo, 12), sx(lo >> 12, 12)
x = (b + 1j * a) if swap else (a + 1j * b)
x = x - x.mean()
print("N=%d  DC I/Q=(%.1f,%.1f)  rms=%.1f  peak=%d  fs=%.0f MHz  dur=%.0f us" %
      (len(x), a.mean(), b.mean(), np.sqrt(np.mean(np.abs(x) ** 2)), np.abs(np.r_[a, b]).max(), fs / 1e6, len(x) / fs * 1e6))
# envelope in 64 blocks
blk = np.abs(x[: len(x) // 64 * 64]).reshape(64, -1)
env = 20 * np.log10(np.sqrt((blk ** 2).mean(1)) + 1e-9)
print("envelope dB (64 blocks):", " ".join("%d" % round(e) for e in env))
# Welch PSD
n = 256; w = np.hanning(n)
segs = x[: len(x) // n * n].reshape(-1, n)
P = np.mean(np.abs(np.fft.fftshift(np.fft.fft(segs * w, axis=1), axes=1)) ** 2, 0)
P = 10 * np.log10(P + 1e-9); P -= P.max()
f = np.fft.fftshift(np.fft.fftfreq(n, 1 / fs)) / 1e6
for i in range(0, n, 8):
    bar = int(max(0, 60 + P[i:i + 8].mean()))
    print("%7.2f MHz %6.1f dB |%s" % (f[i], P[i:i + 8].mean(), "#" * bar))
# image rejection hint: correlation between I and Q
print("corr(I,Q)=%.3f  std I=%.1f Q=%.1f" % (np.corrcoef(a, b)[0, 1], a.std(), b.std()))
