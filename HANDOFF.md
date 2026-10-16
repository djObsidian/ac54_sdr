# HANDOFF — ASUS USB-AC54 (RTL8822BU) SDR via USB

**Session 2 (2026-10-17): RAW RF I/Q CAPTURE WORKS.** Chip is brought up from scratch over
WinUSB/libusb (no Realtek driver, no firmware), tuned to any 2.4/5 GHz channel, and the
LA-mode (ADC sampling) block dumps 16384 × 12-bit I/Q samples @ 40 MS/s per snapshot.
Verified: 802.11 OFDM packet on ch11 (flat ±8.5 MHz, DC-null) visible in spectrum.

## Quick start
```
cd tools
mk.bat init & mk.bat la                    (MSVC + libusb; mk.bat pins Win SDK 10.0.20348.0)
init.exe ch=11                             power-on + efuse + PHY tables + tune (~1 s)
init.exe pwr=0 phy=0 ch=36                 retune only
la.exe dma=0 rate=1 out=cap\x.bin          one snapshot: 16384 samples, path A, 40 MS/s
python iq_view.py cap\x.bin 40e6           envelope + ASCII spectrum
python la2iq.py cap\x.bin cap\x.cs16       -> int16 I/Q (GNU Radio / inspectrum)
reg.exe r 0x7c0 ',' w 0x8fc 0x0 ',' m 0x95c 0xf00 0 ',' b 0x8000 4096   (raw reg tool)
```

## CORRECTIONS to session 1 (important)
- Chip is **RTL8822B** family (rtw88 `rtw8822b*`, phydm "11AC series"), NOT 8812A. SYS_CFG1 (0xF0)
  = 0x0c493d37 → cut D (3), BIT27 → 2T2R. Reg 0x00 is NOT a chip id (it changes).
- `0x8000 = 0xeaeaeaea` was NOT "power off": 0x8000-0x8FFF is a 4 KB **TX-FIFO window**,
  page selected by `0x140[15:0] = 0x780 + (txff_addr >> 12)` (with `0x106 = 0x69`).
- `0x04 BIT8` is APFM_ONMAC (self-clearing), only one step of the real power sequence.
- **H2C was never verified**: no firmware is running (0x1cc = 0x3, boxes stay busy). Not needed.
- BB dbg port for 11AC is **0x8fc** (0x908 is 11N). BB register writes only stick after
  `SYS_FUNC_EN(0x02) |= 0x03` (BB reset released) — that was why 0x908/0x800 writes "failed".
- Source of truth for LA mode is open source: `morrownr/88x2bu-20210702 hal/phydm/phydm_adc_sampling.c`
  (+ `phydm_debug.c`). No need for further Ghidra work on this path.

## Verified facts (hardware)
- Control-transfer register R/W works with **len up to 4096 B in one transfer** (block read/write);
  TXFF 128 KB read ≈ 125 ms (~1 MB/s).
- efuse: id 0x8129, **rfe_option = 2** (iFEM 2.4G / eFEM 5G), xtal_k 0x1d, MAC 4c:ed:fb:b6:ff:d7.
- Bring-up = rtw88 sequence (see `tools/rtwinit.h`): pre_system_cfg → card_enable pwr-seq →
  system_cfg → REG_CR=0xff → BB/RF enable → MAC/BB/AGC/RF-A/RF-B tables (2905 writes, 0.6 s)
  → xtal cap → trx mode (TX A, RX AB) → rfe init → set_channel (bb, mac, rf 0x18, dfir, igi, cca, rfe).
- RF regs readable direct at 0x2800/0x2c00 + addr*4, written via SIPI 0xc90/0xe90. RF0x18 tracks channel.
- LA mode (11AC branch), see `tools/la.c`:
  - trig time: 0x7cc[20:18]=unit, 0x7c0[14:8]=n (µs)
  - BB: 0x8f8[25:22]=hdr, 0x8b4[7]=1, 0x95c[11:8]=dma_type, 0x95c[31]=edge, 0x95c[7:5]=rate (80>>n MHz)
  - dbg port 0x8fc, 0x95c[4:0]=trig_sel
  - 0x7c0: byte0=0, bit0 en, bit3 MAC-trig src, [7:6]=3 (REG trig), bit1 arm/poll,
    then **bit5 = manual trigger** (must be set AFTER arming, else it never finishes)
  - poll 0x7c0 bit1 → 0; report 0x7c0: bit31 round-up, [30:16] finish addr (×8, absolute)
  - ring buffer TXFF 0x20000..0x40000 (half mode), 8 B/sample, read via 0x140 window.
- dma_type formats (active bits, rate=1): 0/1 = 24 bit (12-bit I [11:0] + 12-bit Q [23:12]),
  **0 = path A, 1 = path B** (assumed, as in phydm JGR3 "fast 4"); 4/5 = 48 bit (both paths?);
  6/7 = 40 bit; 8/9 = 20 bit (10+10?); 10 = 32 bit (16+16?); 11 = 64 bit (dbg); 2,3,12-15 = static.
- rate=0 (80 MHz) at BW20 gives every sample duplicated → ADC = 40 MS/s at BW20.
- Snapshot cycle (process start → file) ≈ 100 ms; snapshot length 410 µs @ 40 MS/s.

## Open issues / NEXT STEPS
1. **Gain control**: BB AGC changes RX gain per packet (ch7 capture had rms 0.8 vs 500 elsewhere).
   Need manual gain: freeze AGC / force IGI (0xc50/0xe50 [6:0]) / RF0x00 gain bits. Look at phydm
   `phydm_dig` / rtw88 `rtw_phy_dig_write`, AGC table, 0x800/0xc5x.
2. Confirm I vs Q order (spectral mirroring) with a known CW source (e.g. a BLE/ESP tone,
   or TX of the 2nd adapter). Currently I = bits[11:0], Q = bits[23:12] (unverified).
3. Decode dma 4/5/6/7/8/9/10 formats (dual path, post-DFIR/down-converted?), BW40/80 → 80/160 MS/s?
4. Full-buffer mode (0x7cc BIT30) — phydm says 8822B not in FULL_BUFF list; try anyway (256 KB).
5. Continuous streaming is NOT possible this way (snapshot SDR). For a sweep/waterfall tool:
   loop (retune → trigger → read) and stitch; or investigate RX DMA path (bulk EP 0x84).
6. Tool polish: single `sdr.exe` (init + loop captures), SoapySDR plugin later.

## KEY FILES
- `tools/rtw.h` — libusb open + rd/wr/wmask helpers (EP0 vendor req 0x05).
- `tools/rtwinit.h` — rtw88 8822B bring-up port (pwr seq, efuse, tables, trx mode, channel).
- `tools/t8822b.h` — generated tables (`tools/gen_tables.py` from `ref/rtw88/`).
- `tools/init.c`, `tools/la.c`, `tools/reg.c` — tools; `tools/mk.bat name` builds `name.c`.
- `tools/iq_view.py`, `tools/la_view.py`, `tools/la2iq.py` — analysis/conversion.
- `tools/cap/` — captures (ch1/4/6/7/11/36/149, dma0..15 sweep).
- `ref/rtw88/` — lwfinger/rtw88 clone (reference source; firmware/rtw8822b_fw.bin there too).
- Old session-1 tools (`wakeup.c`, `lasweep.c`, `h2ctest.c`, `powtest.c`, ...) are superseded.
- `docs/usb-map.md` — USB/register protocol notes (updated).
