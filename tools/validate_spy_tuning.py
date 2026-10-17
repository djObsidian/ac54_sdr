"""Check SpyServer digital tuning at three off-raster RF frequencies with Pluto.

Starts its own snapshot-mode server and restores instruments on exit.
Requires built spyserver/init tools, numpy, and libiio CLI on PATH.
"""
import argparse
import json
from pathlib import Path
import socket
import struct
import subprocess
import time

import numpy as np

from measure_response import Pluto, TOOLS, run

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--out', type=Path, required=True)
ap.add_argument('--uri', default='usb:4.1.5')
ap.add_argument('--port', type=int, default=5557)
args = ap.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=False)
port = args.port
rows = []
p = Pluto(args.uri)
server = None
try:
    p.setup(-10)
    with (out / 'server.txt').open('w') as log:
        server = subprocess.Popen([str(TOOLS / 'spyserver.exe'), 'live=0', f'port={port}'],
                                  cwd=TOOLS, stdout=log, stderr=subprocess.STDOUT,
                                  creationflags=subprocess.CREATE_NO_WINDOW)
        time.sleep(2)
        for requested_mhz in (2413.25, 5161.25, 5883.75):
            p.tone(requested_mhz + 1)
            with socket.create_connection(('127.0.0.1', port), timeout=10) as s:
                def cmd(kind, body):
                    s.sendall(struct.pack('<II', kind, len(body)) + body)
                def setting(k, v):
                    cmd(2, struct.pack('<II', k, v))
                def recvn(n):
                    b = bytearray()
                    while len(b) < n:
                        v = s.recv(n - len(b))
                        if not v:
                            raise EOFError()
                        b.extend(v)
                    return bytes(b)
                cmd(0, struct.pack('<I', 33556132) + b'tuning-validation')
                setting(100, 2)
                setting(102, 3)
                encoded = int((requested_mhz - (4000 if requested_mhz > 4000 else 0)) * 1e6)
                setting(101, encoded)
                setting(0, 1)
                setting(1, 1)
                start = time.monotonic()
                chunks = []
                while time.monotonic() - start < 2:
                    proto, kind, stream, seq, size = struct.unpack('<5I', recvn(20))
                    if proto != 33556132 or size > 1048576:
                        raise RuntimeError('Invalid SpyServer message')
                    body = recvn(size)
                    if (kind & 65535) == 101 and time.monotonic() - start > 1:
                        a = np.frombuffer(body, '<i2').astype(float) / 32768
                        chunks.append(a[::2] + 1j * a[1::2])
                setting(1, 0)
            x = np.concatenate(chunks)
            n = 16384
            xx = x[:x.size // n * n].reshape(-1, n)
            power = np.median(abs(np.fft.fftshift(np.fft.fft(
                (xx - xx.mean(axis=1, keepdims=True)) * np.hanning(n), axis=1), axes=1)) ** 2, axis=0)
            fs = 5_000_000
            f = np.fft.fftshift(np.fft.fftfreq(n, 1/fs))
            ix = np.flatnonzero(abs(f - 1e6) < 150_000)
            peak = ix[np.argmax(power[ix])]
            near = (abs(f - f[peak]) > 40_000) & (abs(f - f[peak]) < 200_000)
            snr = float(10*np.log10(power[peak-4:peak+5].sum() / (9*np.median(power[near]))))
            row = dict(requested_mhz=requested_mhz, client_hz=encoded, tone_mhz=requested_mhz+1,
                       fs_hz=fs, peak_hz=float(f[peak]), local_snr_db=snr,
                       samples_analyzed=int(xx.size), passed=bool(snr >= 15 and abs(f[peak]-1e6) < 150_000))
            rows.append(row)
            print(row, flush=True)
            (out / 'results.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
            p.mute()
            time.sleep(0.2)
finally:
    if server is not None:
        server.terminate()
        server.wait(timeout=10)
    try:
        p.close()
    finally:
        (out / 'restore.txt').write_text(
            run([TOOLS / 'init.exe', 'ch=11']).replace('\r\n', '\n') + '\n', encoding='utf-8')
if len(rows) != 3 or not all(r['passed'] for r in rows):
    raise SystemExit(1)
