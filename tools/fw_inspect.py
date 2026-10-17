"""Inspect local vendor RAM firmware layout; does not touch hardware."""
from pathlib import Path
import re
import struct

root = Path(__file__).resolve().parent.parent
s = (root / 'tmp/vendor88x2bu/hal/rtl8822b/hal8822b_fw.c').read_text()
a = s.index('u8 array_mp_8822b_fw_nic[] = {')
b = s.index('};', a)
d = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', s[a:b]))
word = lambda pos: struct.unpack_from('<I', d, pos)[0]
off = 64 + word(36) + 8
print('DMEM', hex(word(32)), word(36), 'IMEM', hex(word(60)), word(48))
print('IMEM first 128 bytes:', d[off:off+128].hex())
(root / 'tmp/vendor-imem.bin').write_bytes(d[off:off+word(48)])
(root / 'tmp/vendor-dmem.bin').write_bytes(d[64:64+word(36)])
print('IMEM first words:', [hex(x[0]) for x in struct.iter_unpack('<I', d[off:off+64])])
print('IMEM entry at 0x200:', d[off+0x200:off+0x240].hex())
for address in [0x8000, 0x8100, 0x8160]:
    print('IMEM', hex(address), d[off+address:off+address+64].hex())
print('DMEM vector bytes at 0x80', d[64+128:64+192].hex())
for addr in [0x18701000, 0xb8701000, 0x18780000, 0xb8780000, 0xb8000000, 0x18600000]:
    print(hex(addr), d.count(struct.pack('<I', addr)))

try:
    import numpy as np
    for name in ['bank-1p25.cs16', 'bank-2p5.cs16']:
        values = np.fromfile(root / 'tools/cap' / name, dtype='<u4')
        print(name, len(values), 'ring-lag equality', float(np.mean(values[16384:] == values[:-16384])))
except ImportError:
    pass
