"""Check RF tuning with muted/+1 MHz/+2 MHz Pluto tones and BW20 snapshots.

Requires numpy, libiio CLI, and built init/reg/la tools. Only one hardware
tool may run at a time. Raw captures stay local; JSON/CSV include the evidence.
This checks reception at individual centers, not sensitivity or filter response.
"""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re

import numpy as np

from measure_response import Pluto, TOOLS, run


FS = 40_000_000
N = 16384


def center_mhz(channel):
    return 2484 if channel == 14 else (2407 if channel <= 13 else 5000) + 5 * channel


def snapshot(path, count):
    log = run([TOOLS / 'la.exe', 'rate=1', f'n={count}', f'out={path}'])
    power, info = read_snapshot(path, count)
    return power, dict(**info, capture=log)


def read_snapshot(path, count):
    raw = np.fromfile(path, '<u8')
    if raw.size != count * N:
        raise RuntimeError(f'{path}: {raw.size} samples, expected {count * N}')
    i = (raw & 4095).astype(np.int32)
    q = ((raw >> 12) & 4095).astype(np.int32)
    i[i >= 2048] -= 4096
    q[q >= 2048] -= 4096
    x = ((i + 1j * q) / 2048).reshape(-1, N)
    window = np.hanning(N)
    pp = abs(np.fft.fftshift(np.fft.fft(
        (x - x.mean(axis=1, keepdims=True)) * window, axis=1), axes=1)) ** 2
    pp /= N * np.sum(window * window)
    return np.median(pp, axis=0), dict(
        file=path.name, samples=int(raw.size),
        clip_fraction=float(np.mean((abs(x.real) >= 0.999) | (abs(x.imag) >= 0.999))))


def tone_metrics(power, muted, offset, search_center=None, search_width=150_000):
    f = np.fft.fftshift(np.fft.fftfreq(N, 1 / FS))
    distance = abs(f - (offset if search_center is None else search_center))
    candidates = np.flatnonzero(distance < search_width)
    peak = candidates[np.argmax(power[candidates])]
    bins = np.arange(peak - 4, peak + 5) % N
    near = (abs(f - f[peak]) > 40_000) & (abs(f - f[peak]) < 200_000)
    signal = float(power[bins].sum())
    floor = float(np.median(power[near]) * len(bins))
    baseline = float(muted[bins].sum())
    return dict(
        peak_hz=float(f[peak]), error_hz=float(f[peak] - offset),
        power_dbfs=float(10 * np.log10(max(signal, 1e-30))),
        local_snr_db=float(10 * np.log10(max(signal, 1e-30) / max(floor, 1e-30))),
        on_off_db=float(10 * np.log10(max(signal, 1e-30) / max(baseline, 1e-30))))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--uri', default='usb:4.1.5')
    ap.add_argument('--channels', default=','.join(map(str, [*range(1, 15), *range(32, 178)])))
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--gain', type=float, default=-20)
    ap.add_argument('--snapshots', type=int, default=3)
    args = ap.parse_args()
    channels = [int(c) for c in args.channels.split(',')]
    if args.snapshots < 1 or not channels or any(c not in [*range(1, 15), *range(32, 178)] for c in channels):
        ap.error('Use channels 1..14 or 32..177 and positive snapshot count')
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    metadata = dict(started_utc=datetime.now(timezone.utc).isoformat(), uri=args.uri,
                    channels=channels, tx_gain_db=args.gain, dds_scale=0.25,
                    fs_hz=FS, bw='BW20', path='A', snapshots_per_state=args.snapshots,
                    offsets_hz=[1_000_000, 2_000_000],
                    criteria=dict(min_local_snr_db=15, min_on_off_db=6,
                                  max_abs_error_hz=150_000, max_movement_error_hz=2 * FS / N))
    metadata['source_revision'] = run(['git', 'rev-parse', 'HEAD'])
    metadata['binary_sha256'] = {name: hashlib.sha256((TOOLS / name).read_bytes()).hexdigest()
                                 for name in ('init.exe', 'la.exe', 'reg.exe', 'libusb-1.0.dll')}
    (out / 'setup.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    p = Pluto(args.uri)
    rows = []
    try:
        p.setup(args.gain)
        for ch in channels:
            fc = center_mhz(ch)
            p.mute()
            init = run([TOOLS / 'init.exe', f'ch={ch}'])
            with (out / 'initialization.txt').open('a', encoding='utf-8') as log:
                log.write(init.replace('\r\n', '\n') + '\n')
            readback = re.search(r'RF18 A=0x([0-9a-f]+) B=0x([0-9a-f]+)', init)
            if not readback:
                raise RuntimeError(f'No channel readback for {ch}')
            rfa, rfb = (int(v, 16) for v in readback.groups())
            run([TOOLS / 'reg.exe', 'm', '0x808', '0x10000000', '0'])
            p.tone(fc + 1, False)
            muted, noise_info = snapshot(out / f'ch{ch:03d}_off.bin', args.snapshots)
            tones = []
            for offset in (1_000_000, 2_000_000):
                p.tone(fc + offset / 1e6)
                power, info = snapshot(out / f'ch{ch:03d}_on{offset // 1_000_000}.bin', args.snapshots)
                tones.append(dict(offset_hz=offset, **info, **tone_metrics(power, muted, offset)))
            p.mute()
            movement_error = tones[1]['peak_hz'] - tones[0]['peak_hz'] - 1_000_000
            passed = ((rfa & 255) == ch and (rfb & 255) == ch
                      and all(t['local_snr_db'] >= 15 and t['on_off_db'] >= 6
                              and abs(t['error_hz']) < 150_000 and t['clip_fraction'] < 0.001 for t in tones)
                      and abs(movement_error) <= 2 * FS / N)
            row = dict(channel=ch, center_mhz=fc, passed=bool(passed),
                       rf18_a=f'0x{rfa:05x}', rf18_b=f'0x{rfb:05x}',
                       movement_error_hz=movement_error, muted=noise_info, tones=tones)
            rows.append(row)
            (out / 'results.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
            print(f"ch={ch:3d} center={fc} MHz {'PASS' if passed else 'UNCONFIRMED'} "
                  f"SNR={[round(t['local_snr_db'], 1) for t in tones]} "
                  f"on/off={[round(t['on_off_db'], 1) for t in tones]} "
                  f"peaks={[round(t['peak_hz'] / 1e6, 5) for t in tones]}", flush=True)
    finally:
        # Restore both instruments even when acquisition or Pluto cleanup fails.
        try:
            p.close()
        finally:
            (out / 'pluto_original.json').write_text(
                json.dumps({str(k): v for k, v in p.saved.items()}, indent=2), encoding='utf-8')
            (out / 'restore.txt').write_text(
                run([TOOLS / 'init.exe', 'ch=11']).replace('\r\n', '\n') + '\n', encoding='utf-8')
    with (out / 'results.csv').open('w', newline='', encoding='utf-8') as f:
        flat = [dict(channel=r['channel'], center_mhz=r['center_mhz'], passed=r['passed'],
                     rf18_a=r['rf18_a'], rf18_b=r['rf18_b'], movement_error_hz=r['movement_error_hz'],
                     **{f'tone{i}_{k}': v for i, t in enumerate(r['tones'], 1) for k, v in t.items() if k not in ('capture',)})
                for r in rows]
        writer = csv.DictWriter(f, fieldnames=list(flat[0]))
        writer.writeheader()
        writer.writerows(flat)
    print(f"Confirmed {sum(r['passed'] for r in rows)}/{len(rows)} centers; instruments restored.")
    metadata['finished_utc'] = datetime.now(timezone.utc).isoformat()
    (out / 'setup.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    return 0 if all(r['passed'] for r in rows) else 1


if __name__ == '__main__':
    raise SystemExit(main())
