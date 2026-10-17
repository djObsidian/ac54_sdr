# ASUS USB-AC54 as an SDR

Experimental raw I/Q reception from an ASUS USB Wi-Fi adapter (`0b05:1841`,
Realtek RTL8822BU family), using WinUSB/libusb and custom RAM firmware.
**20 MSPS streaming has passed a 60-second run** with packet-order, overlap,
deadline and freshness checks. RF performance is still being characterized.

## A little bit of air

[![Recorded 20-MSPS waterfall](docs/media/waterfall.gif)](docs/media/waterfall.mp4)

**[Watch / download the MP4](docs/media/waterfall.mp4)** · 1280×720, 30 fps,
2 seconds of real received IQ at **2462 MHz / BW20**. The GIF is a looping
preview; the recording contains 39,993,344 samples and passed every stream
integrity check. Colors use a fixed scale. [Capture details](docs/media/waterfall-capture.txt).

## Frequency response

![Over-the-air frequency response and aliasing](artifacts/response-24g/response.png)

Pluto TX1 swept **2442–2482 MHz** with the receiver fixed at 2462 MHz:
38 frequencies, forward/reverse passes, and 20/40-MSPS snapshots.
The response falls steeply beyond roughly ±10 MHz. At 20 MSPS, a tone at
**+11 MHz folds to −8.964 MHz**, also verified in the continuous CPU stream.

This is an **uncalibrated over-the-air link response**, including antennas,
multipath and receiver gain behavior. It does not establish receiver-only
ripple or a precise −3 dB bandwidth. The 5-GHz filter response has not been measured.
[Measurement report (Russian)](docs/frequency-response.md) ·
[CSV data](artifacts/response-24g/analyzed.csv)

## Tuning range

The RF tuning code selects these hardware center frequencies:

| Band | Hardware centers | Channel numbers |
| --- | --- | --- |
| 2.4 GHz | **2412–2472 MHz**, 5-MHz steps; **2484 MHz** | 1–13; 14 |
| 5 GHz | **5160–5885 MHz**, 5-MHz steps | 32–177 |

**All 160 centers were confirmed with Pluto test tones on 2026-10-17**, using
BW20 / path A snapshots. The main pass confirmed 159 centers; **5885 MHz**
passed a repeat with 10 dB more TX level. This confirms tone reception on this
adapter, not uniform sensitivity or continuous streaming across the range.
[Results and method](docs/tuning-range.md) ·
[Main CSV](artifacts/response-tuning/analyzed.csv) ·
[Upper-edge repeat](artifacts/response-tuning-edge/analyzed.csv).

These are receiver center frequencies. SpyServer uses the nearest hardware
center and shifts the remaining offset digitally, so its requested frequency
can lie between the 5-MHz steps. For 5 GHz in SDR++, set the source **Offset
to 4000 MHz**: the server interprets client frequencies below 2 GHz as
`frequency + 4 GHz`. This does not provide reception at those lower frequencies.
Direct arbitrary-Hz RF synthesizer tuning and coverage outside the table
have not been established.
SpyServer snapshot-mode checks also passed at **2413.25, 5161.25 and
5883.75 MHz**, including digital shifts in both directions.

**`cpustream` currently stays at 2462 MHz / BW20**; its command-line arguments
do not select the RF center. Use `init.exe ch=N` for standalone snapshots or
SpyServer for interactive tuning. The snapshot checks and their limits are
documented in the [RF tuning report](docs/tuning-range.md).

## Current state

- **20 MSPS:** verified for 60 seconds in the recorded validation run;
  USB scheduling outliers can still stop captures. Checks fail explicitly.
- **40 MSPS:** snapshots and short streaming bursts work; sustained streaming
  is not established.
- **SDR++:** the SpyServer interface works through an earlier capture path;
  the CPU streamer is not integrated into it yet.
- **RF:** tuning checked at all 160 centers above; I/Q orientation and aliasing
  verified at 2.4 GHz. Gain control, calibration and other bandwidths need more work.

## Build and capture

Windows, MSVC, Windows SDK, a WinUSB-bound adapter and USB 3 are required.
The libusb runtime and development files are included in `tools/`.
The CPU firmware also needs local `tmp/vendor-imem.bin`; see
[setup and firmware extraction](HANDOFF.md). Run only one hardware tool at a time.

```bat
cd tools
mk.bat cpustream
cpustream.exe 2 2 cap\capture.cs16 1 1
```

This records two seconds at 20 MSPS with CCK disabled and WinUSB RAW_IO.
Output is interleaved little-endian signed 16-bit I/Q. Firmware is loaded
into RAM; no flash or efuse writes are needed.

For measurement and rendering, use Python with NumPy, Matplotlib, Pillow and
`imageio-ffmpeg`. The entry points are [measure_response.py](tools/measure_response.py)
and [render_waterfall.py](tools/render_waterfall.py).

## Repository map

| Path | Contents |
| --- | --- |
| `tools/` | Device tools, build scripts, analysis scripts and validation logs |
| `docs/` | Protocol notes, measurement report and demo media |
| `analysis/ghidra/` | Driver investigation scripts |
| `artifacts/` | Driver reference files and measurement tables/plots |

[HANDOFF.md](HANDOFF.md) contains detailed findings, commands and limitations.
[PLAN.md](PLAN.md) is the historical plan. Large raw recordings, external
sources and scratch projects stay local in ignored paths; the plots, tables,
logs and demo video are included here.
