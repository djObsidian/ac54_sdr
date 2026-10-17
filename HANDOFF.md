# HANDOFF — ASUS USB-AC54 (RTL8822BU) SDR via USB

## OTA response measured (2026-10-17, latest)

See [docs/frequency-response.md](docs/frequency-response.md) and
`artifacts/response-24g/analyzed.csv`: Pluto TX1 swept 2442..2482 MHz,
RTL center2462/BW20, 38 frequencies x 2 passes x LA rates20/40 MSPS.
RX1 source-level check and two real CPU20-MSPS captures also passed.
Positive RF offset gives positive IQ frequency; +11 MHz aliases to
-8.964 MHz at20 MSPS (also confirmed in CPU stream). Relative oscillator
offset is about +36..38 kHz. Response rolls off strongly beyond +/-10 MHz.
This is an uncalibrated OTA link response: multipath and unverified fixed
RX gain prevent a receiver-only ripple or -3-dB bandwidth claim. The
20-MSPS sampling path is not alias-free. 5 GHz was not measured.
Raw captures remain local/ignored; CSV, JSON, logs and PNG are versionable.
Pluto DDS muted at completion. Initial probe exposed iio_attr -q suppressing
readback; fixed before the saved main sweep; initial Pluto settings restored.

## RF coverage and remaining validation (2026-10-17, before OTA sweep)

- Current `cpustream` is hardcoded to channel11 / 2462 MHz / BW20. Its
  20-MSPS minute-long validation does not validate other RF channels or widths.
- Earlier snapshot files exist for channels1/4/7/11/36/149. Channel11 OFDM
  spectrum was observed; these files are not a calibrated sensitivity or
  filter-response sweep. Do not interpret the historical "any channel" claim
  below as measured coverage of every frequency.
- `spyserver.c` maps 2.4 GHz to channel1..13 (2412..2472 MHz, 5-MHz raster)
  or channel14 (2484 MHz). Its 5-GHz mapping accepts channel32..177
  (5160..5885 MHz, 5-MHz raster), but that software range is not proven usable
  RF coverage. Baseband/RF tables distinguish 36..64, 100..144, and149..177.
  Residual frequency tuning is performed digitally. No out-of-band coverage
  or direct arbitrary-Hz synthesizer tuning has been established.
- `rtwinit.h` has BW5/10/20/40/80 register configurations. Only BW20 capture
  has established ADC behavior (40 MSPS; rate0 repeats samples). Actual filter
  passbands, rejection, and ADC clocks for other bandwidths remain unmeasured.
  Observed Wi-Fi OFDM occupancy around +/-8.5 MHz is not a filter measurement.
- LA rate reduction is sample selection, with no demonstrated anti-alias
  filtering at that tap. A 20-MSPS transport must not be advertised as a
  calibrated, alias-free 20-MHz receiver. Map the tap relative to RF/DFIR and
  measure response/aliasing with a known swept tone.
- Remaining: RF channel/bandwidth sweep and gain control; I/Q orientation,
  DC/IQ imbalance and clock calibration; dual-path formats; integrate the CPU
  streaming path into SpyServer (currently the older DDMA path); expose channel
  and width in the new streamer, with explicit validity bounds and retune gaps.

## Active investigation: working CPU firmware, RAW_IO, live 40 MSPS bursts (2026-10-17)

Custom RAM firmware now works: preserve vendor IMEM startup through 0x230,
including CP0 initialization, and append MIPS16e2 code using LUI/ADDI addresses.
`tools/cpufw.h` uploads and verifies IMEM, starts CPU, and supports recovery.
No flash or efuse writes. `fw_inspect.py` produces the required ignored
`tmp/vendor-imem.bin` from the pinned local vendor NIC source.

`cpustream.c` schedules opposite-bank C2H using hardware microsecond counter
0x568 (CPU address 0xb8640568). CPU arms ADC and accumulates fractional bank
periods exactly. MAC RX remains enabled. Host checks deadlines, packet order,
overlap, and per-ring freshness before emitting any bank.

Validated before RAW_IO: **5 MSPS for 30 seconds**, 149,995,520 samples,
29.999104 RF seconds / 30.000504 wall seconds, max bank 1239 us,
91,550 overlap checks and 18,308 freshness checks. `tools/cpu-5-30s.txt`.
CCK-on at 10 MSPS produced entire banks of constant IQ; these are rejected.
Optional CCK disable (0x808 bit28 clear) removes that observed failure mode.

Critical USB improvement: original DLL 1.0.23 lacked RAW_IO. Official libusb
1.0.30 VS2022 MS64 DLL is now at `tools/libusb-1.0.dll`; old DLL saved at
`tmp/libusb-1.0.23-backup.dll`. Download is the official GitHub v1.0.30 7z.
DLL SHA256: 7CBF37E76DAE9C840C7E8DBF7348EE8897DCC86C8BA45E46ADA60B89411569F7.
`cpubench 10000 16128 1`: **402.415 MB/s**, every PRBS packet verified, versus
~295 MB/s without RAW_IO. This benchmark is static memory, not live ADC.
RAW_IO uses 65536-byte host transfers; API is dynamically resolved, so old
import library still works. System driver is unchanged.

With RAW_IO + CCK off, original seven-packet CPU streamer passed 3 seconds
at both **10 and 20 MSPS** (`cpu-raw-10.txt`, `cpu-raw-20.txt`).
The five-packet streamer also passed **20 MSPS for 30 seconds**:
599,998,464 samples, 29.999923 RF / 30.001263 wall seconds, max bank 274 us,
292,968 overlaps and 73,240 freshness checks (`cpu-raw-20-30s.txt`).
Current profiled build captured `tools/cap/cpu-20-raw.cs16`: 59,998,208
samples / 2.999910 RF seconds; max bank 250 us (`cpu-20-file.txt`).
Final profiled build passed **20 MSPS for 60 seconds**:
1,199,996,928 samples, 59.999846 RF / 60.001338 wall seconds, max bank325 us,
585,936 overlap checks, 146,482 freshness checks, CPU error0.
Log: `tools/cpu-20-60s.txt`. CPU and PHY restored afterward.
Independent saved-file check: 59,998,208 samples, one-ring-lag IQ equality
0.0000007335556, 6,243,934 unique IQ values (`cpu-20-file-check.txt`).
This recording is not a frozen-ring replay. `git diff --check` passes.
At 40 MSPS, six-packet variant passed **0.661504 s / 26,460,160 samples**
before a deadline failure (`cpu-raw-40-guard2.txt`). This is NOT sustained
40 MSPS success. Current optimization uses four overlapping data packets
plus one combined saved-prefix/timing packet; start/end guards are 2% each.
QDEPTH=32; RAW_IO and process/thread priority raised. Queues128/256 did not help.
Latest five-packet variants still hit occasional deadlines within <1 s.
Do not disable deadline or freshness checks to claim success.

Concrete blocker measured by the on-chip timer: C2H packets normally take
38..45 us, but one took 369 us (`cpu-raw-40-profile.txt`), another 325 us
with 256 queued host reads (`cpu-raw-40-q256.txt`). This exceeds the entire
204.8-us opposite-bank window at 40 MSPS. RAW_IO fixes average throughput,
not these outliers. USB/DMA aggregation modes both returned separate packets
and ~402 MB/s, with no improvement (`cpu-agg1.txt`, `cpu-agg2.txt`).
Disabling USB U1/U2 via standard CLEAR_FEATURE after initialization, verified
by GET_STATUS changing 000c->0000, still failed the deadline at bank1146
(`cpu-raw-40-nolpm-verified.txt`). Original feature state is restored on exit.
Current host transfer buffer is 16384 bytes, packet payload16352, saved prefix
224 bytes, first direct offset200, overlap24 bytes. Largest packet16376 bytes
is short of a full USB transfer, avoiding an extra zero-length completion.

CPU packing measured separately in `cpupackbench.c`: discarding upper 32 bits
of each 64-bit word reaches only 3.803 MSPS for clock selectors 0/2 and
2.799 MSPS for 1/3. All outputs PRBS-verified. Software packing on this path
cannot carry 40 MSPS. Next work: improve USB scheduling/headroom or identify
hardware buffering/aggregation that does not collide with active LA bank.

Commands from `tools`: `mk.bat cpustream`, then
`cpustream RATE SECONDS OUTPUT NOCCK RAW [DISABLE_USB_U1_U2]`,
e.g. `cpustream 2 30 NUL 1 1`.
RAW defaults to 1. Rate1=40M, rate2=20M, rate3=10M, rate4=5M.
Each experiment resets CPU/PHY afterward. Never access hardware concurrently.

## C2H bank collision isolated and avoided (2026-10-17)

`corruptprobe.c` isolates descriptor writes from C2H, with MAC RX enabled.
At rate=6, it snapshots a stable 512-byte range through EP0 before/after each
operation. Baseline reads and descriptor-only writes: 20 trials each, zero
payload changes. C2H can return different IQ AND leave zero words in SRAM.
Stopping LA before C2H removes this effect (40 trials, zero mismatches).
More specifically, at 16 ms after arming, ADC writes bank 0:
- `corruptprobe 2 50 0 0`: 45 SRAM words changed, 44 bulk mismatches.
- `corruptprobe 2 50 0 1`: zero SRAM or bulk mismatches in the opposite bank.
RXDMA arbitration bit 0 at 0x10c did not fix same-bank corruption.
Initial rate=7 diagnostic was invalid: it did not provide the assumed stable
26-ms ring. Current diagnostic uses rate=6 only and rejects late trials.

New `bankstream.c` reads each completed 64-KiB bank while ADC writes the other.
The prefix comes through EP0 so no C2H descriptor crosses into the active bank.
Each direct packet has 32 exact-overlap samples; per-bank freshness and fixed
start/completion deadlines are enforced. No pauses, filler, or rate hiding.
Transport helpers extracted to `directio.h`, also used by `faststream.c`.

Validated `bankstream rate=5 seconds=30 async=1 out=NUL`:
**74,997,760 samples; 29.999104 s RF / 30.000490 s wall; max bank fetch
2.240 ms; 45,775 overlap checks; 9,153 freshness checks.**
Log: `tools/bank-2p5-30s.txt`. This is new real 2.5 MSPS evidence; the old
frozen-ring runs below remain retracted. Three-second files:
`cap/bank-1p25.cs16` (3,743,744 samples) and `cap/bank-2p5.cs16`
(7,495,680 samples); independent one-ring-lag equality fractions
0.0000013414 and 0.0000005348 respectively.
At this earlier host-scheduled stage, 5 MSPS exceeded the bank deadline (~1.33 ms to fetch
against 1.31 ms available), so it is not validated. 40 MSPS remains open.

The next path at that stage (now implemented above) was embedded CPU scheduling:
access/packing speed. `cpuprobe.c` is RAM-only and always resets CPU/PHY after
testing. IMEM DDMA upload/readback is exact. Tiny custom MIPS16/MIPS32 stubs
have NOT produced a marker yet. Positive control loading the local vendor
NIC IMEM+DMEM DOES boot to MCUFW_CTRL=0xC078, so the download route works.
`fw_inspect.py` extracts the NIC image from the pinned local vendor source
into ignored `tmp/vendor-{imem,dmem}.bin` for analysis. No flash/efuse writes.
The failed initial stub attempts in this historical paragraph were subsequently
resolved by preserving vendor CP0 startup, as described at the top.

## Critical correction: frozen-ring false positives (2026-10-17)

**Read this first. Previous direct-live 2.5/5 MSPS claims below are retracted.**
Disabling MAC RX (`CR 0x100 bit7`) freezes the ADC writer on this device.
Exact overlap checks alone accepted the resulting repeated SRAM contents.
In `tools/cap/direct-2p5m.cs16`, 99.9837% of IQ samples equal the sample one
16,384-sample ring earlier. This was not a live 2.5 MSPS recording.
Earlier DDMA live evidence with MAC RX disabled is likewise insufficient.

`iqstream.h` now keeps MAC RX enabled and rejects ordinary Wi-Fi packets via
RCR and RX filter maps 0x6a0/0x6a2/0x6a4. It restores these registers on exit.
New `iqfresh.h` compares low 24-bit IQ at each address against the previous
revolution, ignoring metadata; >=95% identical samples rejects the block.
Near-constant input is deliberately considered unverified freshness.
This supplements, rather than replaces, exact overlap and deadline checks.
`test_iqfresh.c` rejects frozen and sparsely modified rings and accepts an
advancing synthetic stream. Both iqstream and SpyServer use this check.

Revalidated with MAC RX enabled and freshness checks:
- `iqstream rate=6 seconds=30 out=NUL`: 37,499,584 samples,
  29.999667 s RF / 30.005762 s wall, max fetch 1.765 ms; PASS.
  Log: `tools/ddma-fresh-30s.txt`.
- Three-second file `tools/cap/ddma-fresh.cs16`: 3,749,760 samples;
  one-ring-lag equality 0.1005%, versus 99.9837% in the old direct file.
- Rebuilt SpyServer: `test_live.ps1 -Seconds 3` PASS, 3,755,712 samples,
  measured 1,248,452 samples/s, 100 distinct initial packet hashes.

**At this checkpoint: 1.25 MSPS; superseded by the bank-scheduled 2.5 MSPS above.**
The direct LA alias and stopped-SRAM throughput below remain valid, but
`faststream` is diagnostic only: with the ADC running, individual words
change (often to zero) between overlapping reads, even at 1.25 MSPS.
`guard=1` compares the first 256 payload bytes before/after descriptor write
and after C2H; observed a mutation during the C2H interval at offset 0xf800.
This localizes an observation, not the underlying cause; other mutations
occur before that interval. It must not be bypassed or repaired by filling.
Descriptor bytes 12..23 can change in hardware; packet verification checks
the first 12 plus length and status. OWN-bit clearing did not fix corruption;
the established OWN=1 default has been restored.

Negative experiments: disabling MAC TX, holding the embedded CPU, clearing
descriptor OWN, and RXDMA_MODE low-byte values 0/2/12/14 did not fix live
corruption. Disabling RXDMA or setting RW_RELEASE prevented C2H completion.
`faststream dmamode=0|2|12|14` retains the reproducible mode experiment;
default is 14. Async EP0 submission did not establish a working live rate.
`ringprobe full=0` and `full=1` with an active ADC changed only upper 128 KiB
of TX SRAM: 0x7cc bit30 did not extend the ring on this device. Both runs
had positive controls (>100 KiB changed in the normal ring).
Next investigation: isolate descriptor writes from C2H reads while the ADC
runs, and identify the source of sparse SRAM mutations. Retain MAC RX and
freshness checks in every future live experiment.

## Historical direct LA SRAM -> USB experiments (live claims superseded above)

**DDMA can be bypassed.** `routeprobe.c` found a direct C2H alias of LA SRAM;
`lamap.c` verified every byte in all 32 distinct 4 KiB pages of the 128 KiB LA
ring with LA stopped. Physical USB SRAM order differs from OCP/window order:

| TX SRAM offset | C2H address (0x294) |
| --- | --- |
| 0x30000..0x3ffff | 0x06000..0x15fff |
| 0x20000..0x2ffff | 0x16000..0x25fff |

For address `a`, request `(a & 0xffff) | ((a >> 16) << 24) | 0x10000`.
The request points at a 24-byte RX descriptor, followed by payload. Prior
attempts using the OCP-relative TX address were wrong. A sweep of high address
nibbles showed RX aliases at 0x01000/0x41000/0x81000/0xc1000; this alone did
not discover TX. The lower-address sweep found the LA marker inside a packet
starting at C2H=0x15000, then direct C2H=0x17000 returned exactly the marker
at TX=0x21000. Logs: `tools/route-*.txt`, `tools/la-map.txt`.

`membench la=1 n=2000`: 2000 verified 16128-byte payloads at **155.794 MB/s**
direct from LA SRAM. This is memory-only throughput, not live ADC. No DDMA.

**Correction to old handoff:** FWFF request 0x29c bit31 IS defined for 8822B,
including the chip-specific `halmac_bit_8822b.h`. `routeprobe route=1` with
0x29c=0x80011000 returns the unique marker at firmware RX SRAM 0x18681000,
while normal RX SRAM remains unchanged. `membench fw=1 bytes=7936 n=2000`:
90.035 MB/s without DDMA; with `dma=1`, 13.191 MB/s and 497.6 us average DDMA.
This alternate output works but does not accelerate TX->FW copying.
Vendor source pinned at d31ffa827bb95b8a436c2a469b5163b634ac4333 in ignored
`tmp/vendor88x2bu`, from https://github.com/morrownr/88x2bu-20210702 .

New experimental `tools/faststream.c`: continuous LA -> direct C2H -> cs16,
with 1984 new samples / 32 exact-overlap samples per fetch. It writes an RX
descriptor immediately BEFORE each fetched interval, into already-consumed
samples (or a region the ADC will rewrite before future consumption). At
C2H=0x6000 the descriptor is in the last 24 bytes of ordinary RX SRAM.
It splits reads at the alias discontinuity and ring end, never triggers LA,
and stops on malformed packets, overlap mismatch, or writer deadline failure.
Full initialization/reinitialization occurs before/after; channel is fixed 11.
Output is LE int16 I,Q, 12-bit values scaled by 16. `out=-` is NOT supported.
Ctrl+C stops cleanly. Existing `iqstream`/SpyServer transport is unchanged.

Build: `cd tools`, `mk.bat faststream`. Example:
`faststream rate=5 seconds=30 out=cap\direct.cs16` (2.5 MS/s).
Rate=4 is 5 MS/s; rate=3/2/1 request 10/20/40 MS/s.
Initial 5 MS/s / 3 s run passed 14,999,040 samples, max fetch 0.926 ms.
Longer 5 MS/s attempts stopped on scheduling lateness after 4.46 s and 2.40 s;
raising process priority did not solve it and was removed. Initial 10/20/40
MS/s attempts failed deadlines almost immediately. Do NOT call these rates
sustained. The final reader permits catching up within the same conservative
0.45-ring completion deadline; it never shifts the sample clock to hide gaps.

Final validation:
- **2.5 MS/s, 30 s to NUL: PASS**, 74,999,168 samples, 29.999667 s RF /
  30.002611 s wall, max block work 1.281 ms, all overlap checks passed.
- **2.5 MS/s, 3 s to cs16 file: PASS**, 7,499,520 samples / 29,998,080 bytes,
  max block work 0.910 ms. Saved as `tools/cap/direct-2p5m.cs16`.
- Final 5 MS/s file attempt stopped after 13,649,920 accepted samples
  (2.729984 s RF), reporting host late 2.375 ms. Partial capture remains at
  `tools/cap/direct-5m.cs16`; it is NOT a completed 30-second capture.
- `faststream` defaults to tested rate=5 (2.5 MS/s); output defaults to NUL.
  USB3 status remained intact. `git diff --check` passed.

The new experimentally demonstrated 30-second live rate is **2.5 MS/s**,
superseding the 1.25 MS/s limit of the DDMA implementation below. This is not
a guarantee of indefinite operation. Logs: `tools/fast-*.txt`.

Next: optimize descriptor publication/USB queuing or move it to firmware;
the old DDMA bottleneck is no longer fundamental to capture transport.
40 MS/s still requires 320 MB/s raw, or device-side packing (120 MB/s for
12+12-bit IQ). Vendor LA source inspected so far supplies no compact packing
mode or alternate ADC destination for 8822B. CPU-side packing throughput and
custom firmware remain unmeasured. Do not claim 40 MS/s or loss-free operation
under arbitrary host scheduling load.

## DDMA matrix follow-up (2026-10-17; superseded by direct route above)

Added `tools/ddmabench.c`: build `mk.bat ddmabench`, run `ddmabench n=30`.
This is a **memory-only** test, not ADC streaming. Each copy uses fresh PRBS,
verifies source and inverse destination before DMA, then verifies the complete
destination after DMA. Setup/readback are excluded from timing; the EP0 command
and completion polling are included. MAC TX/RX and LA capture are stopped.
Hardware is reinitialized after each run. No production capture changes.

Tested RX (0x18700000), TX (0x18780000), and LA-region TX (0x187a0000),
all nine directions, 256/4096/8192/16128 bytes; RX->RX omits 16128 because
two disjoint blocks do not fit the documented 24 KiB RX SRAM. Baseline:
1050 verified copies, every supported cell passed. Large-block times are
964-977 us (~16.5-16.7 MB/s); RX->RX at 8192 B is 544.5 us. LA->RX is
973.0 us / 16.576 MB/s. No special penalty for TX->RX or the LA SRAM region.
This supports a shared DDMA-path bottleneck, not proof of its hardware ceiling.

Controlled experiments (each full matrix; logs in `tools/ddma-*.txt`):

- `cpu=1 n=10`: MCUFW_CTRL[13:12]=1 sticks, but slows large copies to
  ~1045-1054 us. `cpu=2` reads back selector 0; `cpu=3` reads back 1.
  These two runs abort before timing, restore the old register, and reinitialize.
  Do not assign MHz to selector values: the actual frequency is unverified.
- `debug=0 n=10`: clear 0x106 during copying, restore 0x69 for verification.
  No speedup (~971-978 us).
- `gap=200 n=10`: busy-wait 200 us before each OWN read, reducing EP0 poll
  traffic from ~22 to ~4 reads per large copy. No speedup; observed completion
  is ~1.10-1.25 ms, including deliberate polling quantization.
- `reset=1 n=20`: vendor `pltfm_reset_88xx` sequence (0x1082 bit0 off,
  0x09 bit6 off, 0x1082 bit0 on, 0x09 bit6 on), explicitly described by
  Realtek as a 8822B/8821C clock-sync fix. No improvement: LA->RX 971.5 us.
  Source: https://github.com/morrownr/88x2bu-20210702/blob/main/hal/halmac/halmac_88xx/halmac_fw_88xx.c

Note: an initial exploratory matrix used RX destinations beyond the documented
24 KiB region. Its results were discarded; the saved baseline and current tool
use bounded, disjoint ranges. Do not infer additional RX capacity from readback.

After experiments, USB3 status is intact and `iqstream ch=11 rate=6 seconds=2
out=NUL` passed all overlaps: 2,499,840 samples, max fetch 1.409 ms.
**40 MS/s remains unachieved; proven live rate remains 1.25 MS/s.**

Next: prioritize hypothesis 2/3 below (bypass DDMA / compact capture format).
Inspect chip-qualified vendor definitions for alternate memory routing and
diagnostic modes; do not reuse 8814B/8822C-only registers on 8822B. If revisiting
DDMA, measure firmware SRAM / IMEM / DMEM paths with documented address bounds
and verification before considering a packing firmware. The matrix above tests
packet SRAM paths, not embedded CPU throughput or all possible buses.

## Throughput investigation (2026-10-17)

`tools/membench.c` isolates USB export from TX-to-RX DDMA. Build with
`tools/mk.bat membench` from tools. Run `membench.exe n=2000 dma=0` versus
`membench.exe n=2000 dma=1`. These repeatedly export a fixed PRBS block,
**not live ADC samples**. Each received packet is compared byte-for-byte;
DMA tests initialize the RX payload to the inverse of the expected TX payload.
The tool reinitializes the device before and after each run.

Measured USB3/WinUSB, payload 16128 bytes:

- Ready RX SRAM -> USB -> host: 158.390 MB/s (2000 blocks).
- TX SRAM -> DDMA -> RX SRAM -> USB: 15.110 MB/s (2000 blocks).
- DDMA stage averages 964 us; control request averages 45 us, bulk wait 41 us.
- DDMA at 4096/8192 bytes averages 289/537 us: substantial size-dependent cost.
- Payload alignment at 32/64 bytes, DDMA_MODE bit 26, and channel 1 instead of
  channel 0 did not materially improve throughput (~15.1 MB/s total).
- Runtime DLL is libusb 1.0.23.11457; RAW_IO API is unavailable in this DLL.

This corrects the earlier hypothesis that EP0 command overhead alone limits
2.5 MS/s. DDMA currently dominates. At 8 bytes per LA sample, 15.1 MB/s is only
~1.89 MS/s without overlap/timing margin; the proven live rate remains 1.25 MS/s.
The 158 MB/s memory export benchmark is neither a physical USB maximum nor proof
of live IQ at 19.8 MS/s. At the observed 40 MS/s BW20 sample rate, raw LA requires
320 MB/s; hypothetical packed 12+12-bit IQ needs 120 MB/s, but on-chip packing
and a faster/bypass capture route have NOT been implemented. The maximum unique
ADC sample rate across wider channel configurations remains unverified.

### Next experiments, in priority order

User wants continuous IQ onto the PC, ideally limited by USB3 throughput or the
chip sample rate. SpyServer is optional and is not the current bottleneck.
The following are hypotheses to test, not established hardware capabilities:

1. Explain slow DDMA: benchmark different source/destination SRAM regions and
   block sizes, verifying copied data. Compare bus clocks, memory arbitration,
   and initialization with the vendor driver before changing documented fields.
   Separate TX read, RX write, and general DDMA limits where possible.
2. Bypass TX -> RX copying: investigate USB DMA access to capture SRAM, address
   aliases, or routing capture into USB-accessible memory. The simple direct
   TX C2H request already failed; do not present this route as working.
3. Search vendor diagnostic/ADC modes for an alternative continuous IQ path or
   a more compact hardware capture format. Current LA stores 8 bytes per sample
   for 3 useful IQ bytes. No alternative mode has been established yet.
4. Consider custom firmware only after measuring embedded CPU/memory throughput.
   It could manage buffers and possibly pack IQ on-device, but software packing
   at 40 MS/s is unproven and firmware alone cannot fix a slow DDMA bus.
5. Pipeline with double buffering and multiple USB requests once the internal
   path is understood. This can overlap stages and reduce gaps, but cannot
   overcome the measured DDMA throughput limit. Host-side packing cannot reduce
   traffic already transferred through DDMA/USB.

Immediate next experiment: a verified memory-to-memory DDMA throughput matrix,
then clock/configuration comparison and direct USB access investigation. Keep
memory-only benchmarks distinct from live ADC tests; validate any improvement
with sustained capture and continuity checks before claiming a higher IQ rate.

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
  No working ADC-to-RX-bulk route was identified in that session. Correction:
  FWFF request bit31 at 0x29c DOES support 8822B and now works (see top).
  RX_DBG_SEL bit7 at 0x290 still lacks 8822B qualification in these headers.
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
