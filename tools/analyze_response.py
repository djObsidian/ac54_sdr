"""Reanalyse saved response snapshots with a local noise estimate and known IQ sign."""
import argparse
import csv
import json
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def analyze(path, fs, offset, clock_offset=36_500):
    raw = np.fromfile(path, '<u8')
    i = (raw & 4095).astype(np.int32)
    q = ((raw >> 12) & 4095).astype(np.int32)
    i[i >= 2048] -= 4096
    q[q >= 2048] -= 4096
    x = ((i+1j*q)/2048).reshape(-1, 16384)
    n = x.shape[1]
    w = np.hanning(n)
    pp = abs(np.fft.fftshift(np.fft.fft((x-x.mean(axis=1, keepdims=True))*w, axis=1), axes=1))**2 / (n*np.sum(w*w))
    p = np.median(pp, axis=0)
    f = np.fft.fftshift(np.fft.fftfreq(n, 1/fs))
    target = (offset+clock_offset+fs/2)%fs-fs/2
    dist = abs((f-target+fs/2)%fs-fs/2)
    ix = np.flatnonzero(dist<12e3)[np.argmax(p[dist<12e3])]
    bins = np.arange(ix-4, ix+5)%n
    near = (dist>40e3)&(dist<200e3)
    noise = np.median(pp[:, near], axis=1)*9
    powers = np.maximum(pp[:, bins].sum(axis=1)-noise, 1e-20)
    power = float(np.median(powers))
    snr = float(10*np.log10(power/(np.median(noise)+1e-30)))
    return dict(power_dbfs=float(10*np.log10(power)), snr_db=snr,
                detected=snr>=10, peak_mhz=float(f[ix]/1e6),
                snapshot_spread_db=float(np.ptp(10*np.log10(powers))),
                local_floor_dbfs=float(10*np.log10(np.median(noise)+1e-30)))


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory', type=Path)
    args=ap.parse_args()
    rows=json.loads((args.directory/'results.json').read_text())
    for r in rows:
        r.update(analyze(args.directory/r['file'], r['fs_mhz']*1e6, r['offset_mhz']*1e6))
    (args.directory/'analyzed.json').write_text(json.dumps(rows, indent=2))
    with (args.directory/'analyzed.csv').open('w', newline='') as f:
        writer=csv.DictWriter(f, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    fig, axes=plt.subplots(2, 1, figsize=(11, 8), sharex=True)
    for rate in sorted({r['rate'] for r in rows}):
        for rep in sorted({r['repeat'] for r in rows}):
            rr=sorted([r for r in rows if r['rate']==rate and r['repeat']==rep], key=lambda r:r['offset_mhz'])
            xx=[r['offset_mhz'] for r in rr]
            axes[0].plot(xx, [r['power_dbfs'] if r['detected'] else np.nan for r in rr], '.-', label=f'{80/2**rate:g} MSPS pass {rep+1}')
            axes[1].plot(xx, [r['peak_mhz'] if r['detected'] else np.nan for r in rr], '.-')
    axes[0].set(ylabel='Tone power (dBFS)', title='2462 MHz / BW20: uncalibrated OTA response (SNR >= 10 dB)')
    axes[1].set(xlabel='TX offset from receiver center (MHz)', ylabel='Measured IQ tone (MHz)')
    for ax in axes:
        ax.grid(); ax.axvline(-10, color='grey', ls='--'); ax.axvline(10, color='grey', ls='--')
    axes[0].legend(); fig.tight_layout(); fig.savefig(args.directory/'response.png', dpi=160)
    good=[r for r in rows if r['detected'] and abs(r['offset_mhz'])<=9]
    print('Detected passband range dBFS:', min(r['power_dbfs'] for r in good), max(r['power_dbfs'] for r in good))
    print('Strong out-of-Nyquist tones:', [(r['rate'],r['offset_mhz'],round(r['peak_mhz'],3),round(r['power_dbfs'],1)) for r in rows if r['detected'] and abs(r['offset_mhz'])>10])


if __name__=='__main__':
    main()
