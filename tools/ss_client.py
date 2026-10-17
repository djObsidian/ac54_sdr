"""Minimal SpyServer client (mimics SDR++) to test tools/spyserver.exe.
usage: ss_client.py [freq_hz] [decim] [seconds] [fmt 2=int16|1=uint8|4=float]
"""
import socket, struct, sys, time, numpy as np
freq = int(float(sys.argv[1])) if len(sys.argv) > 1 else 2462000000
decim = int(sys.argv[2]) if len(sys.argv) > 2 else 2
secs = float(sys.argv[3]) if len(sys.argv) > 3 else 2.0
fmt = int(sys.argv[4]) if len(sys.argv) > 4 else 2
VER = (2 << 24) | (0 << 16) | 1700

s = socket.create_connection(("127.0.0.1", 5555))
def cmd(t, body): s.sendall(struct.pack("<II", t, len(body)) + body)
def setting(k, v): cmd(2, struct.pack("<II", k, v))
def recvn(n):
    b = bytearray()
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c: raise EOFError
        b += c
    return bytes(b)
def msg():
    proto, mtype, stream, seq, size = struct.unpack("<5I", recvn(20))
    return mtype, stream, seq, recvn(size)

cmd(0, struct.pack("<I", VER) + b"ss_client")
t, _, _, body = msg(); assert t == 0, t
di = struct.unpack("<12I", body)
print("DEVICE_INFO", dict(zip("type serial maxSR maxBW decimStages gainStages maxGain fmin fmax res minDecim forced".split(), di)))
t, _, _, body = msg(); print("SYNC", struct.unpack("<9I", body))
setting(100, fmt); setting(102, decim); setting(101, freq); setting(0, 1); setting(2, 0)
setting(1, 1)
fs = di[2] / (1 << decim)
iq = []; n = 0; t0 = time.time(); seqs = []
while time.time() - t0 < secs:
    t, st, seq, body = msg()
    if t & 0xffff == 101: a = np.frombuffer(body, "<i2").astype(np.float32) / 32768
    elif t & 0xffff == 100: a = (np.frombuffer(body, np.uint8).astype(np.float32) - 128) / 128
    elif t & 0xffff == 103: a = np.frombuffer(body, "<f4")
    else: print("msg", t, len(body)); continue
    x = a[0::2] + 1j * a[1::2]; n += len(x); iq.append(x)
dt = time.time() - t0
setting(1, 0); s.close()
x = np.concatenate(iq)
print("received %d samples in %.2f s -> %.2f MS/s (expected %.2f)" % (n, dt, n / dt / 1e6, fs / 1e6))
N = 1024; seg = x[: len(x) // N * N].reshape(-1, N)
P = 10 * np.log10(np.mean(np.abs(np.fft.fftshift(np.fft.fft(seg * np.hanning(N), axis=1), axes=1)) ** 2, 0) + 1e-12)
P -= P.max(); f = np.fft.fftshift(np.fft.fftfreq(N, 1 / fs)) / 1e6
for i in range(0, N, N // 32):
    v = P[i:i + N // 32].mean(); print("%8.2f MHz %6.1f |%s" % (f[i], v, "#" * int(max(0, 50 + v))))
