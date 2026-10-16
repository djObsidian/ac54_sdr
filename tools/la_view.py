"""Quick look at an LA-mode dump (la.bin: 8-byte samples, [lo32][hi32])."""
import sys, numpy as np
fn = sys.argv[1] if len(sys.argv) > 1 else "la.bin"
d = np.fromfile(fn, dtype="<u4").reshape(-1, 2)
lo, hi = d[:, 0].astype(np.int64), d[:, 1].astype(np.int64)
print("samples", len(d), " unique hi:", np.unique(hi)[:8], " unique lo:", len(np.unique(lo)))
dup = np.mean(lo[0::2] == lo[1::2])
print("pairwise duplicate fraction: %.3f" % dup)

def sx(v, bits):
    v = v & ((1 << bits) - 1)
    return np.where(v >= (1 << (bits - 1)), v - (1 << bits), v)

# bit activity
act = [(np.mean((lo >> b) & 1), np.mean(((lo >> b) & 1)[1:] != ((lo >> b) & 1)[:-1])) for b in range(32)]
print("lo bit: p(1)/toggle")
for b in range(31, -1, -1):
    print("  b%02d %.2f %.2f" % (b, act[b][0], act[b][1]))
hact = [np.mean((hi >> b) & 1) for b in range(32)]
print("hi bits set:", [b for b in range(32) if hact[b] > 0.01])

for split in (10, 11, 12):
    a, b = sx(lo, split), sx(lo >> split, split)
    print("split %d: A mean %.1f std %.1f min %d max %d | B mean %.1f std %.1f min %d max %d" %
          (split, a.mean(), a.std(), a.min(), a.max(), b.mean(), b.std(), b.min(), b.max()))
