"""Relative over-the-air response using Pluto TX1 DDS and RTL LA snapshots.

Requires numpy, matplotlib and libiio command-line tools on PATH. Run --help.
All instruments are accessed sequentially. TX is muted in finally.
"""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import sys
import time
import numpy as np

TOOLS = Path(__file__).resolve().parent


def run(args, binary=False):
    p = subprocess.run([str(x) for x in args], cwd=TOOLS, capture_output=True, timeout=45)
    if p.returncode:
        raise RuntimeError(f"{args}: {p.stderr.decode(errors='replace')} {p.stdout[:500]!r}")
    return p.stdout if binary else p.stdout.decode(errors='replace').strip()


class Pluto:
    def __init__(self, uri):
        self.uri, self.saved = uri, {}

    def attr(self, dev, ch, name, value=None, output=True):
        key = (dev, ch, name, output)
        args = ['iio_attr', '-u', self.uri, '-o' if output else '-i', '-c', dev, ch, name]
        if value is not None:
            if key not in self.saved:
                self.saved[key] = run(args).removesuffix(' dB')
                if not self.saved[key]:
                    raise RuntimeError(f'Empty attribute read: {key}')
            args.append(str(value))
        return run(args)

    def mute(self):
        for ch in range(4):
            self.attr('cf-ad9361-dds-core-lpc', f'altvoltage{ch}', 'scale', 0)

    def setup(self, gain):
        self.mute()
        self.attr('ad9361-phy', 'voltage0', 'hardwaregain', gain)
        self.attr('ad9361-phy', 'voltage0', 'rf_port_select', 'A')
        self.attr('ad9361-phy', 'altvoltage1', 'powerdown', 0)
        for ch, phase in [(0, 90000), (2, 0)]:
            for name, value in [('frequency', 1000000), ('phase', phase), ('raw', 1)]:
                self.attr('cf-ad9361-dds-core-lpc', f'altvoltage{ch}', name, value)

    def tone(self, mhz, on=True):
        # Fixed 1-MHz DDS offset keeps the TX baseband response constant.
        self.attr('ad9361-phy', 'altvoltage1', 'frequency', round((mhz - 1) * 1e6))
        for ch in (0, 2):
            self.attr('cf-ad9361-dds-core-lpc', f'altvoltage{ch}', 'scale', 0.25 if on else 0)
        time.sleep(0.08)

    def close(self):
        self.mute()
        errors = []
        for (dev, ch, name, output), value in reversed(list(self.saved.items())):
            if dev == 'cf-ad9361-dds-core-lpc' and name == 'scale':
                continue  # Always leave RF tone muted, even if it was on before.
            try:
                self.attr(dev, ch, name, value, output)
            except Exception as exc:
                errors.append(str(exc))
        if errors:
            raise RuntimeError('Restore failed: ' + '; '.join(errors))


def spectrum(x, fs, expected):
    n = 16384
    x = x[:len(x)//n*n].reshape(-1, n)
    w = np.hanning(n)
    p = np.mean(abs(np.fft.fftshift(np.fft.fft((x-x.mean(axis=1, keepdims=True))*w, axis=1), axes=1))**2, axis=0) / (n * np.sum(w*w))
    f = np.fft.fftshift(np.fft.fftfreq(n, 1/fs))
    alias = (expected + fs/2) % fs - fs/2
    # Search both signs until orientation has been established from observations.
    mask = (abs(f-alias)<120e3) | (abs(f+alias)<120e3)
    ix = np.flatnonzero(mask)[np.argmax(p[mask])]
    bins = (np.arange(ix-4, ix+5) % n)
    noise = float(np.median(p))
    power = max(float(p[bins].sum()-len(bins)*noise), 1e-20)
    return dict(tone_dbfs=10*np.log10(power), peak_mhz=f[ix]/1e6,
                snr_db=10*np.log10(power/(len(bins)*noise+1e-30)),
                rms_dbfs=10*np.log10(np.mean(abs(x)**2)+1e-30),
                clip_fraction=float(np.mean((abs(x.real)>=0.999)|(abs(x.imag)>=0.999))))


def capture(path, rate, expected, count=8):
    log = run([TOOLS/'la.exe', f'rate={rate}', f'n={count}', f'out={path}'])
    raw = np.fromfile(path, '<u8')
    i = (raw & 4095).astype(np.int32)
    q = ((raw >> 12) & 4095).astype(np.int32)
    i[i >= 2048] -= 4096
    q[q >= 2048] -= 4096
    result = spectrum((i+1j*q)/2048, 80e6/2**rate, expected)
    result['capture'] = log
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--uri', default='usb:4.1.5')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--gain', type=float, default=-40)
    ap.add_argument('--offsets', default='-18,-15,-12,-10,-9,-8,-6,-4,-2,1,2,4,6,8,9,10,12,15,18')
    ap.add_argument('--rates', default='2,1')
    ap.add_argument('--repeats', type=int, default=2)
    args = ap.parse_args()
    offsets = [float(v) for v in args.offsets.split(',')]
    if args.repeats < 1 or not set(args.rates.split(',')) <= {'1', '2'}:
        ap.error('Use positive repeats and snapshot rates 1 (40 MSPS) or 2 (20 MSPS)')
    if any(not 2400 <= 2462+v <= 2483.5 for v in offsets):
        ap.error('This measurement is restricted to 2400..2483.5 MHz')
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    p = Pluto(args.uri)
    rows = []
    try:
        p.setup(args.gain)
        (out/'init.txt').write_text(run([TOOLS/'init.exe', 'ch=11']), encoding='utf-8')
        run([TOOLS/'reg.exe', 'm', '0x808', '0x10000000', '0'])
        for rate in map(int, args.rates.split(',')):
            p.tone(2463, False)
            base = capture(out/f'noise_r{rate}.bin', rate, 1e6)
            (out/f'noise_r{rate}.json').write_text(json.dumps(base, indent=2))
            for rep in range(args.repeats):
                for offset in (offsets if rep%2 == 0 else offsets[::-1]):
                    p.tone(2462+offset)
                    filename = f'r{rate}_rep{rep}_{offset:+07.2f}.bin'
                    result = capture(out/filename, rate, offset*1e6)
                    regs = run([TOOLS/'reg.exe', 'r', '0xc50', ',', 'r', '0xe50', ',', 'r', '0x2800'])
                    row = dict(rate=rate, fs_mhz=80/2**rate, repeat=rep, offset_mhz=offset,
                               rf_mhz=2462+offset, tx_gain_db=args.gain, file=filename, registers=regs, **result)
                    rows.append(row)
                    with (out/'results.json').open('w') as f:
                        json.dump(rows, f, indent=2)
                    print(f"{rate=} {rep=} {offset=:+6.1f} power={result['tone_dbfs']:.2f} peak={result['peak_mhz']:+.3f} SNR={result['snr_db']:.1f}", flush=True)
    finally:
        p.close()
        (out/'pluto_original.json').write_text(json.dumps({str(k):v for k,v in p.saved.items()}, indent=2))
    with (out/'results.csv').open('w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    print(run([sys.executable, TOOLS/'analyze_response.py', out]))


if __name__ == '__main__':
    main()
