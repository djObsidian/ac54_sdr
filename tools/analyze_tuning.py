"""Reanalyse tuning snapshots, using +2-MHz carrier to locate +1-MHz carrier.

Some +1-MHz captures contain a stronger nearby sideband. The second tone
anchors the common oscillator error; search within two FFT bins of its
predicted +1-MHz location. Preserve original acquisition results unchanged.
"""
import argparse
import csv
import json

from pathlib import Path

from validate_tuning import FS, N, read_snapshot, tone_metrics


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory', type=Path)
    args = ap.parse_args()
    out = args.directory
    setup = json.loads((out / 'setup.json').read_text(encoding='utf-8'))
    rows = json.loads((out / 'results.json').read_text(encoding='utf-8'))
    limits = setup['criteria']
    flat = []
    for row in rows:
        muted, _ = read_snapshot(out / row['muted']['file'], setup['snapshots_per_state'])
        t1, t2 = row['tones']
        p2, _ = read_snapshot(out / t2['file'], setup['snapshots_per_state'])
        t2.update(tone_metrics(p2, muted, t2['offset_hz']))
        p1, _ = read_snapshot(out / t1['file'], setup['snapshots_per_state'])
        predicted = t2['peak_hz'] - (t2['offset_hz'] - t1['offset_hz'])
        t1['acquisition_peak_hz'] = t1['peak_hz']
        t1.update(tone_metrics(p1, muted, t1['offset_hz'], predicted, 2 * FS / N))
        t1['search_center_hz'] = predicted
        t1['search_half_width_hz'] = 2 * FS / N
        row['acquisition_passed'] = row['passed']
        row['movement_error_hz'] = t2['peak_hz'] - t1['peak_hz'] - 1_000_000
        row['passed'] = (all((int(row[key], 16) & 255) == row['channel'] for key in ('rf18_a', 'rf18_b'))
                         and all(t['local_snr_db'] >= limits['min_local_snr_db']
                                 and t['on_off_db'] >= limits['min_on_off_db']
                                 and abs(t['error_hz']) < limits['max_abs_error_hz']
                                 and t['clip_fraction'] < 0.001 for t in (t1, t2))
                         and abs(row['movement_error_hz']) <= limits['max_movement_error_hz'])
        flat.append(dict(channel=row['channel'], center_mhz=row['center_mhz'], passed=row['passed'],
                         rf18_a=row['rf18_a'], rf18_b=row['rf18_b'],
                         movement_error_hz=row['movement_error_hz'],
                         **{f'tone{i}_{key}': t[key] for i, t in enumerate((t1, t2), 1)
                            for key in ('peak_hz', 'error_hz', 'power_dbfs', 'local_snr_db', 'on_off_db', 'clip_fraction')}))
    (out / 'analyzed.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
    missing = sorted(set(setup['channels']) - {r['channel'] for r in rows})
    summary = dict(expected_centers=len(setup['channels']), acquired_centers=len(rows),
                   confirmed_centers=sum(r['passed'] for r in rows), missing_channels=missing,
                   unconfirmed_channels=[r['channel'] for r in rows if not r['passed']],
                   analysis='Anchor at +2 MHz; search +1-MHz carrier within two FFT bins of common oscillator offset',
                   criteria=limits)
    (out / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    with (out / 'analyzed.csv').open('w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=list(flat[0]))
        writer.writeheader()
        writer.writerows(flat)
    print(f"Confirmed {sum(r['passed'] for r in rows)}/{len(rows)} centers.")
    print('Unconfirmed channels:', [r['channel'] for r in rows if not r['passed']])
    if missing:
        print('Missing channels:', missing)


if __name__ == '__main__':
    main()
