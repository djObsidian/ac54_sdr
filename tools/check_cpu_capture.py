from pathlib import Path
import sys
import numpy as np

cap = Path(__file__).resolve().parent / 'cap'
bad = cap / 'cpu-frozen.bin'
if bad.exists():
    raw = np.fromfile(bad, dtype='<u8')
    v = raw & 0xffffff
    values, counts = np.unique(v, return_counts=True)
    print('Frozen bank unique IQ', len(values), 'most common', [(hex(int(values[i])), int(counts[i])) for i in np.argsort(counts)[-5:]])
    print('First 8 raw', [hex(int(x)) for x in raw[:8]])
for name in sys.argv[1:] or ['cpu-2p5.cs16', 'cpu-5.cs16', 'cpu-10.cs16', 'cpu-10-retry.cs16', 'cpu-20.cs16', 'cpu-20-raw.cs16']:
    if not (cap/name).exists():
        continue
    data = np.fromfile(cap/name, dtype='<u4')
    if len(data)>16384:
        print(name, 'samples', len(data), 'lag identical', np.mean(data[16384:] == data[:-16384]), 'unique', len(np.unique(data)))
        if bad.exists():
            i = (v & 4095).astype(np.int16);i[i>=2048]-=4096
            q = ((v >> 12) & 4095).astype(np.int16);q[q>=2048]-=4096
            converted = np.empty((len(v),2),dtype='<i2');converted[:,0]=i*16;converted[:,1]=q*16
            words=converted.view('<u4').ravel()
            for off in [8192,16384]:
                if len(data)>=off: print(' match frozen against tail offset',off,np.mean(data[-off:-off+8192 if off!=8192 else None]==words))
