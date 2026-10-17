# HANDOFF — ASUS USB-AC54 (RTL8822BU) SDR via USB

## Session 5 (2026-10-17): continuous IQ reaches PC at 1.25 MS/s

User priority: get continuous IQ onto the PC; SpyServer is only a viewer adapter.
Previous checkpoint committed as `eb1aa8e` (USB3 + snapshot SpyServer).

**Working device-side path, no firmware:**

1. Arm LA once (`0x7c0=0xcb`), never trigger it. It keeps overwriting TX SRAM
   `0x20000..0x3ffff`; `0x7c0` does NOT expose a live writer cursor.
2. DDMA (`0x1200/04/08`) copies samples from `0x18780000 + LA_START + offset`
   to RX SRAM `0x18701018`. Register writes can be batched into one 12-byte
   vendor write; source/destination precede OWN/length. Poll OWN until done.
3. A 24-byte RX descriptor at `0x18701000` contains payload length, OWN and C2H
   flags. Request `0x294=0x00011000` sends descriptor + payload to bulk EP84.
4. Host bulk read is submitted asynchronously before the copy/request.
   Capture stays armed throughout; no snapshot repetition or crossfades.

`tools/dmaprobe.c` proved exact transfer of a deterministic 256-byte marker,
including DDMA copy TX -> RX. Direct C2H from TX and firmware RX space did not
return the marker in the tried mappings; only normal RX staging is established.

`tools/liveprobe.c` measured ring wrap time. **Correction: rate=7 is not a usable
625 kS/s divider on this chip** (observed rapid wraps, consistent with fast ADC).
rate=6 gives ~13.107 ms/ring = **1.25 MS/s**. Earlier minimum-rate claims below
are superseded. Raw LA words are 8 B each -> ~10 MB/s transported plus overlap.

Standalone usage (does its own full initialization to reset stale RX FIFO state):

```
cd tools
mk.bat iqstream
usb3.exe status
iqstream.exe ch=11 rate=6 seconds=30 out=cap\continuous.cs16
```

Output: little-endian signed int16 I,Q; 12-bit ADC values scaled by 16.
`out=-` produces **binary stdout**, all diagnostics go to stderr. This is the
primary integration path for another PC application; no SpyServer dependency.
For binary redirection under Windows PowerShell 5 use cmd, e.g.
`cmd /c "iqstream.exe seconds=2 out=- > cap\stdout.cs16"`.

Each fetch has 1984 new samples and 32 overlapping samples. Success requires exact
byte agreement of each overlap with the previous fetch. The reader starts half
a ring behind the writer, paces via QPC, and stops on late scheduling, missed DMA
deadline, overlap mismatch, unexpected USB packet, or output error. It does not
silently repeat/drop/fill samples. Timing is based on nominal sample clock;
unbounded loss-free operation under arbitrary Windows load is NOT established.
Reinitialization/retuning starts a new capture and has a gap.

Verified:
- Standalone 30 s: **37,499,584 samples**, 29.999667 s RF / 30.005890 s wall,
  all overlap checks passed, max fetch 2.198 ms; file 149,998,336 bytes.
- After batched DMA commands + prequeued USB read: standalone 10 s,
  12,499,200 samples, all overlap checks passed, max fetch 1.947 ms.
- Binary stdout: 2,499,840 samples / 9,999,360 bytes, no text in output.
- **2.5 MS/s (rate=5) is not sustained by this implementation**, even without
  SpyServer and after batching/prequeueing. Host misses the ring deadline after
  a few blocks and exits nonzero. Not a demonstrated USB3 link bandwidth limit.
  DDMA_MODE bit26 did not resolve it and is left clear.
- LA rate division is sample dropping, not a verified anti-alias decimator;
  BB remains BW20. Do not interpret 1.25 MS/s output as a clean 1.25 MHz RF band.
  Higher-throughput transport and hardware filtering/packing remain next work.

Optional viewer: `spyserver.exe` now defaults to `live=1`: advertises 1.25 MS/s,
zero extra decimation, bounded queue, never replays samples, closes client on
capture/queue failure. `live=0` explicitly selects the old snapshot-loop mode.
Frequency retune restarts acquisition (~0.7 s gap); phase/DC removal are stateful
inside each continuous run. The DLL/USB path is shared with standalone iqstream.
`test_live.ps1` checks protocol sequencing, rate, 100 distinct blocks, format,
and optional retune. int16: 10 s ~1,249,475 samples/s; float + retune passed;
uint8 after final transport changes: 3 s ~1,252,283 samples/s, 100 distinct blocks.

## Session 4 (2026-10-17): USB3 verified; continuous IQ NOT implemented

- Built and ran `tools/usb3.exe` (rtw88 switch sequence). Hardware now reports
  **bcdUSB=0300, EP0=512, EP84 maxpacket=1024, SS companion present,
  0xff=0x20, 0xc4=0x00080400**. `usb3.exe status` is read-only.
- Windows rebound the new enumeration to `netrtwlanu.inf` (Wi-Fi), so libusb
  returned NOT_SUPPORTED. Reapplied the **existing** Zadig package `oem16.inf`
  with `bind_winusb.exe C:\Windows\INF\oem16.inf` through UAC. Package number
  is machine-specific: inspect `pnputil /enum-devices /instanceid
  "USB\VID_0B05&PID_1841\123456" /drivers` before reusing on another PC.
  No reboot was needed. Replug can return to USB2; run usb3 again and then init.
- Full `init.exe ch=11` succeeded. **SuperSpeed EP0 reads >=512 B failed**
  (512 -> STALL, 1024/4096 -> timeout); 504/508 B worked. `la.h` now uses
  504-byte chunks on USB3 and 4096 on USB2, selecting each TXFF page once.
  20/20 snapshots: 1203 ms; 100/100: 5750 ms, 13,107,200 bytes;
  SHA256 comparison confirmed 100 distinct snapshot blocks.
  This is ~17 snapshots/s, NOT the previously predicted 30-50+.
- `la.exe` now reports completed captures and exits nonzero on capture/file errors.
- `mk.bat` discovers installed MSVC/SDK (this PC has VS18 and SDK 10.0.26100.0).
  Rebuilt usb3, bind_winusb, bulkprobe, la, spyserver successfully.
  SpyServer smoke test over localhost received three int16 IQ messages at
  decimation 5 after retuning to 2462 MHz (still snapshot-loop semantics).
- `bulkprobe.exe seconds=5 la=0` queues 8 x 64 KB asynchronous EP84 transfers,
  sets RX DMA burst size for current USB speed and MAC RX filters, and restores
  changed registers after cancellation. One run received 20,187 bytes in 65
  completions, initially only: RX descriptors + 802.11 frames, **not IQ**.
- `bulkprobe.exe seconds=5 la=1` additionally arms LA without firing a trigger:
  **0 bytes**, LA register `0x800064cb` at end. This tests a candidate route,
  not all possible hardware configurations; it does not prove impossibility.
- WinUSB supports continuous asynchronous bulk reads. The USB3 feature named
  "bulk streams" (stream IDs) is a different feature and is not required here.
  Sources: https://learn.microsoft.com/en-us/windows/win32/api/winusb/nf-winusb-winusb_readpipe
  and https://techcommunity.microsoft.com/blog/microsoftusbblog/using-winusb-for-usb-3-0-superspeed-devices-on-windows-8/270761/
- Realtek LA source and HALMAC definitions examined:
  https://github.com/morrownr/88x2bu-20210702/blob/main/hal/phydm/phydm_adc_sampling.c
  and `hal/halmac/halmac_bit2.h`, `halmac_reg2.h` (downloaded under ignored tmp/).
  The known 8822B path captures into TX packet RAM and reads after trigger stop.
  No working ADC-to-RX-bulk route identified. FWFF request bit31 at 0x29c and
  RX_DBG_SEL bit7 at 0x290 are defined for other chips, **not 8822B**: do not
  assume those newer-chip mechanisms apply here.
- **Outstanding user request:** genuine continuous IQ, without repeated snapshots.
  Requires discovering/implementing a device-side route from ADC/TX RAM to USB
  with overrun and continuity verification, potentially custom firmware.
  Host queuing alone does not provide that route. Existing SpyServer still loops
  snapshots; its startup now explicitly says so. Do not call this real-time RF.
  Minimum documented LA rate 80/128 MHz needs 5 MB/s at 8 B/sample, already above
  measured EP0 snapshot readout (~2.3 MB/s including capture overhead).

## Historical session 3 / original USB3 plan (superseded above)

**Session 3 (2026-10-17): WORKS IN SDR++.** `tools/spyserver.exe` pretends to be a SpyServer;
SDR++ (Source = SpyServer, 127.0.0.1:5555) shows live spectrum/waterfall, retune from the UI works
(checked by the user). **NEXT TASK: switch the chip to USB 3.0** (see section right below).

## ▶ NEXT: switch to USB 3.0 (SuperSpeed)

Bottleneck now = reading the 128 KB LA buffer through EP0 control transfers at USB 2.0:
~125 ms/snapshot → ~11.5 snapshots/s (snapshot = 410 µs of air). On SuperSpeed EP0 (512 B packets,
lower latency) expect ~10–20 ms → 30–50+ snapshots/s → much denser waterfall.
(Still snapshot-based: continuous 40 MS/s is impossible over EP0 regardless of USB speed.)

Facts (hardware + rtw88 `usb.c`):
- RTL8812BU = USB variant of 8822B; Linux `rtw8822bu` lists `0b05:1841` ("ASUS USB-AC55 B1").
  Realtek USB3 chips ALWAYS enumerate as USB 2.0 first; the driver switches them to USB 3.
- Current state: `REG_SYS_CFG2+3 (0xFF) = 0x80` (≠ 0x20 → High Speed),
  `REG_PAD_CTRL2 (0xC4) = 0x00100000` (BIT20 USB3_USB2_TRANSITION = can switch, [19:18]=0 never tried),
  efuse logical 0x06 = 0xa1 → bit7 `usb_mode_switch = 1` (allowed).
- Procedure = `rtw_usb_switch_mode_new()` (ref/rtw88/usb.c:1146), to implement as `usb3.exe`:
  ```
  if (rd8(0xFF) == 0x20) -> already USB3, done
  pad = rd32(0xC4); require pad & (BIT20 | 3<<18)
  if (((pad >> 18) & 3) == 2) -> already tried: port is USB2 only
  wr8(0x10C3, rd8(0x10C3) & ~1)            // REG_SW_MDIO+3: enable IO wrapper timeout (8822B)
  pad = (pad & ~(3<<18)) | (2<<18) | BIT16 // USB23_SW_MODE = U3, RSM_EN_V1
  wr32(0xC4, pad); wr8(0xC5, 4)
  wr16(0x04, rd16(0x04) | BIT9)            // APFM_OFFMAC
  Sleep(1); wr32(0xC4, rd32(0xC4) | BIT17) // NO_PDN_CHIPOFF_V1
  -> device disconnects and re-enumerates as SuperSpeed (USB transfers may error out: expected)
  ```
- After switch: check descriptors (bcdUSB 3.x, maxpacket 1024 bulk, EP0 512), WinUSB binding
  (Windows may treat it as a new device instance → re-run Zadig for `0b05:1841` if needed),
  then re-run full `init.exe` (power cycle sequence) and measure `la.exe n=20` time.
- rtw88 then also does `rtw_usb_phy_cfg(rtwdev, USB_SPEED_SUPER)` (usb3_para table from
  `phy_para_table_8822b`) — port it if transfers are unstable. Replug returns the chip to USB 2.0.
- Other possible speedups after USB3: async/queued control transfers (libusb async), check if
  the device accepts >4096 B per transfer (window is 4 KB, so probably not useful).

## SDR++ usage (works)
```
cd tools
mk.bat spyserver
spyserver.exe                 (does full chip init itself; init=0 to skip; port=5555)
```
SDR++: Source = SpyServer, host 127.0.0.1, port 5555, Connect, pick sample rate
(40 / 20 / 10 / 5 / 2.5 / 1.25 MS/s), Play. 2.4 GHz: tune directly.
5 GHz: SpyServer frequencies are uint32 → set Source "Offset" = 4000 MHz (server maps f < 2 GHz → f + 4 GHz).
Implementation (`tools/spyserver.c`): capture thread (retune to 5 MHz channel raster → LA snapshot →
DC removal → digital shift by residual → windowed-sinc FIR decimation → loop buffer with crossfaded
ends); network thread streams the newest loop buffer at real-time rate (snapshot repeated until the next
one). Formats int16 / uint8 / float. No gain control (MaximumGainIndex = 0) yet.
Protocol test client: `python ss_client.py 2462e6 1 2` (freq, decim, seconds).

---

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
