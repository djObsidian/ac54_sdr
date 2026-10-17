"""Check OTA level linearity, Pluto RX1 reference, and actual 20-MSPS CPU stream."""
import json
from pathlib import Path
import numpy as np
from measure_response import Pluto, run, capture, spectrum, TOOLS
from analyze_response import analyze


def main():
    out=TOOLS.parent/'artifacts'/'response-validation'
    out.mkdir(exist_ok=False)
    p=Pluto('usb:4.1.5')
    rows=[]
    try:
        p.setup(-20)
        p.attr('ad9361-phy', 'voltage0', 'gain_control_mode', 'manual', False)
        p.attr('ad9361-phy', 'voltage0', 'hardwaregain', 10, False)
        p.attr('ad9361-phy', 'voltage0', 'rf_port_select', 'A_BALANCED', False)
        for gain in [-20, -30]:
            p.attr('ad9361-phy', 'voltage0', 'hardwaregain', gain)
            run([TOOLS/'init.exe', 'ch=11'])
            run([TOOLS/'reg.exe', 'm', '0x808', '0x10000000', '0'])
            for offset in [1, 4, 8, 10, 11, 12]:
                p.tone(2462+offset)
                path=out/f'gain{gain}_offset{offset}.bin'
                capture(path, 2, offset*1e6)
                r=dict(tx_gain_db=gain, offset_mhz=offset, **analyze(path, 20e6, offset*1e6))
                # RX1 monitors the source at the same +1-MHz baseband offset each time.
                p.attr('ad9361-phy', 'altvoltage0', 'frequency', int((2461+offset)*1e6))
                raw=run(['iio_rwdev', '-u', p.uri, '-b', '16384', '-s', '65536', 'cf-ad9361-lpc', 'voltage0', 'voltage1'], True)
                (out/f'rx1_gain{gain}_offset{offset}.cs16').write_bytes(raw)
                a=np.frombuffer(raw, '<i2').astype(float).reshape(-1, 2)
                r['pluto_rx1']=spectrum((a[:,0]+1j*a[:,1])/2048, 30.72e6, 1e6)
                rows.append(r)
                (out/'levels.json').write_text(json.dumps(rows, indent=2))
                print('LEVEL', gain, offset, round(r['power_dbfs'],2), 'RX1', round(r['pluto_rx1']['tone_dbfs'],2), flush=True)
        p.attr('ad9361-phy', 'voltage0', 'hardwaregain', -20)
        cpu=[]
        for offset in [1, 11]:
            p.tone(2462+offset)
            path=out/f'cpu_offset{offset}.cs16'
            log=run([TOOLS/'cpustream.exe', '2', '1', path, '1', '1'])
            (out/f'cpu_offset{offset}.txt').write_text(log)
            a=np.memmap(path, '<i2', mode='r')[:2*16384*32].astype(float).reshape(-1,2)
            result=spectrum((a[:,0]+1j*a[:,1])/32768, 20e6, offset*1e6)
            cpu.append(dict(offset_mhz=offset, **result))
            print('CPU', offset, result, flush=True)
        (out/'cpu.json').write_text(json.dumps(cpu, indent=2))
    finally:
        p.close()
        (out/'pluto_original.json').write_text(json.dumps({str(k):v for k,v in p.saved.items()}, indent=2))


if __name__=='__main__':
    main()
