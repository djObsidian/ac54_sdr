"""Convert an LA dump (dma=0/1: 12-bit I [11:0], 12-bit Q [23:12]) to interleaved IQ.
usage: la2iq.py in.bin out.cs16|out.cf32 [--dedup]   (cs16 = int16 I,Q ; cf32 = float32 I,Q scaled to +-1)
"""
import sys, numpy as np
src, dst = sys.argv[1], sys.argv[2]
lo = np.fromfile(src, dtype="<u4").reshape(-1, 2)[:, 0].astype(np.int32)
if "--dedup" in sys.argv:
    lo = lo[0::2]
def sx(v, b):
    v = v & ((1 << b) - 1); return np.where(v >= (1 << (b - 1)), v - (1 << b), v)
i, q = sx(lo, 12), sx(lo >> 12, 12)
if dst.endswith(".cf32"):
    np.stack([i, q], 1).astype(np.float32).__itruediv__(2048.0).tofile(dst)
else:
    np.stack([i, q], 1).astype(np.int16).tofile(dst)
print("wrote %d complex samples to %s" % (len(i), dst))
