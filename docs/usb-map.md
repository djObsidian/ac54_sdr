# USB-карта ASUS USB-AC54 (RTL8812BU)

**Обновление 2026-10-17:** USB3 включён утилитой `tools/usb3.exe` и проверен
на устройстве: bcdUSB=0300, EP0=512, bulk=1024, SS companion, регистр 0xff=0x20.
После переопределения восстановлена привязка WinUSB. Таблицы USB2 ниже описывают
исходное состояние. В USB3 чтения vendor EP0 на 512/1024/4096 байт не работают;
504 байта работают, что учтено в `la.h`. Непрерывный ADC → bulk IQ пока не получен:
асинхронное чтение EP84 работает, но обнаруженные данные — Wi-Fi-кадры.
Подробности и команды — в начале `HANDOFF.md`.

Снято: pyusb (libusb-1.0) + ctypes, 2026-10-16.

## Идентификация

| Поле | Значение |
|---|---|
| VID | `0x0B05` (ASUSTeK) |
| PID | `0x1841` |
| bcdDevice | `0x0210` |
| InstanceID | `USB\VID_0B05&PID_1841\123456` |
| Bus / Address | `1` / `12`, port `8` |
| bDeviceClass | `0x00` (defined at interface) |
| bcdUSB | `0x0210` (USB 2.1) |
| bMaxPacketSize0 | 64 |
| Pwr | 500 mA |

## Конфигурация

- **1 конфигурация** (value=1), `wTotalLength=53` — ровно `9(cfg)+9(if)+5×7(EP)`,
  **без SuperSpeed-companion-дескрипторов (0x30)** → устройство работает по **USB 2.0 High Speed**.
- **1 интерфейс**, vendor-specific (`class=0xFF sub=0xFF proto=0xFF`).

> libusb/хаб сообщают `speed=SUPER (5Gbps)`, но это скорость USB3-хаба;
> по дескрипторам (bcdUSB 0x0210, bulk 512, нет companion) устройство — **USB2 480 Мбит/с**.

## Endpoints (5)

| EP | Направление | Тип | maxpkt | Примечание |
|---|---|---|---|---|
| `0x84` | IN  | **BULK** | 512 | RX-данные (IQ/MPDU) |
| `0x05` | OUT | **BULK** | 512 | TX-команды / данные |
| `0x06` | OUT | **BULK** | 512 | TX-данные |
| `0x08` | OUT | **BULK** | 512 | TX-команды (cmd/H2C) |
| `0x87` | IN  | INT    | 64  | interrupt (C2H/статус) |

**Итого: 4 BULK (1 IN + 3 OUT) + 1 INT IN.**

## Выводы для SDR

- USB **2.0 High Speed** → реальный пропускной канал ~**40–50 МБ/с** (480 Мбит/с минус оверхед).
  - 16-бит IQ (I+Q) при **10 MS/s** = 40 МБ/с — впритык влезает.
  - 16-бит IQ при **20 MS/s** = 80 МБ/с — уже **не влезает** в USB2.
  - 80 MS/s (номинал из стрингов) = 320 МБ/с — только при USB3.
- **Важно:** если чип реально USB3, но адаптер работает в USB2-режиме — надо проверить,
  можно ли переключить в SuperSpeed (реестр/пин-аут/прошивка), иначе потолок ~10 MS/s.
- 3 OUT bulk-канала = классическая схема Realtek: cmd / tx-data / (3-й — tx-agg или h2c).

## ✅ USB register R/W protocol (CRACKED, 2026-10-17)

Register R/W — это **USB control-запросы на endpoint 0** (не bulk!).
Протокол снят из open-source rtw88 (`usb.c` / `usb.h`), подтверждён на железе.

| операция | bmRequestType | bRequest | wValue | wIndex | data |
|---|---|---|---|---|---|
| **READ** reg | `0xC0` (vendor, D2H) | `0x05` | `addr` | `0x0000` | read `len` B |
| **WRITE** reg | `0x40` (vendor, H2D) | `0x05` | `addr` | `0x0000` | write `len` B (value) |

Константы из rtw88: `RTW_USB_CMD_READ=0xc0`, `RTW_USB_CMD_WRITE=0x40`,
`RTW_USB_CMD_REQ=0x05`, `RTW_USB_VENQT_CMD_IDX=0x00`.

libusb:
```c
// read
libusb_control_transfer(h, 0xC0, 0x05, addr, 0, buf, len, 1000);
// write
libusb_control_transfer(h, 0x40, 0x05, addr, 0, valbuf, len, 500);
```

**Подтверждено на железе (WinUSB + libusb, claimed):**
- `reg 0x04` (4B) = `0x20030892`, `reg 0xF0` (SYS_CFG1) = `0x0c493d37` → cut D, 2T2R (BIT27).
  (`reg 0x00` — НЕ chip id, значение меняется.)
- Длина одного control-transfer — **до 4096 байт** (блочное чтение/запись подряд идущих адресов).
- ~~`0x8000 = 0xeaeaeaea` = idle ADC~~ — **неверно**: 0x8000..0x8FFF — окно 4 КБ в TX-FIFO,
  страница = `0x140[15:0] = 0x780 + (addr>>12)` (при `0x106 = 0x69`). Без выбора страницы читается 0xea.

### LA-mode (IQ dump) register sequence (from Ghidra RE of rtwlanu.sys)

Readout (`FUN_1403a3c40`): samples = reg `0x8000+off` (I) / `0x8004+off` (Q), step +8,
into `param_1[0x1663]` buffer. Trigger:

| step | func | register | value |
|---|---|---|---|
| set dbg_port (route ADC) | `FUN_14038bc70` | `0x8fc` (11AC: 8822B) / `0x908` (11N) / `0x1c3c` (JGR3) | dbg_port |
| trigger time | `FUN_1403a44e0` | `0x7cc` | shift |
|  |  | `0x7c0` | `(trig>>shift)&0x7f` (mask 0x7f00) |
| MAC IQ dump enable | `FUN_1403a42d0` | `0x7c0` | bit0=1, then `0xc0 & dbg_port` |
| BB config | `FUN_1403a3fa0` | `0x95c`/`0x9a0` | is_fake/trig/smp_rate |
| poll ready | `FUN_1403a3920` | `0x7c0[7:0]` | until `& 0xfe` clears |
| readout | `FUN_1403a3c40` | `0x106`=0x69, then `0x8000/0x8004` loop | I/Q |

> Superseded by the working sequence in the "RF I/Q capture working" section below.

## ✅ Off-domain power-on + H2C mailbox (2026-10-17)

### Register sections (confirmed on hardware, rtw88 `usb.c`)
- **on** (0x00~0xFF, 0x1000~0x10FF): always powered.
- **off** (<0xFE00, excl. "on"): powered off in IPS → reads 0xeaeaeaea, writes lost.
- **local** (>=0xFE00): USB-specific.
- 8812A does NOT need the extra 0x4e0 write (only 8822C/B/21C).

### Off-domain power-on (SOLVED)
The chip is in **IPS** (inactive power save, "Disconnected"). To power on the "off"
domain (mailbox, BB/PHY, sample buffer):

```c
uint32_t pw = read32(h, 0x0004);              // REG_SYS_PW_CTRL
write32(h, 0x0004, (pw & ~0x300) | 0x100, 4); // set BIT8 (MAC_ENABLE), clear BIT9 (MAC_OFF)
```
After: mailbox 0x1cc=0, RFMOD 0x800=0x9020d010, PSD 0x910=0xfc00, ADCCLK 0x8ac,
DBGSEL 0x8fc=0x130 — all live. Register writes in "off" domain now stick.

### H2C mailbox commands (SOLVED, rtw88 `fw.c`/`fw.h`)
8-byte command (w0 main + w1 ext) to REG_HMEBOX0-3 (0x1d0..0x1dc) + EX (0x1f0..0x1fc).
Sequence: poll REG_HMETFR (0x1cc) for box free → write EX (w1) → write main (w0, triggers).

| command | id | w0 encoding (byte0=cmd id) |
|---|---|---|
| SET_PWR_MODE | 0x20 | mode<<8, rlbm<<16, smart_ps<<20, awake<<24; w1: port<<5, state<<8 |
| WL_CH_INFO | 0x66 | link<<8, chnl<<16, bw<<24 |
| RA_INFO | 0x40 | macid<<8, rate<<16, ... |
| RSSI_MONITOR | 0x42 | macid<<8, rssi<<24 |

> **Session 2 corrections:** chip = RTL8822B (rtw88 `rtw8822b`), not 8812A. The `0x04 BIT8`
> write is only one step of the real power sequence (now `tools/rtwinit.h`, ported from rtw88).
> H2C mailbox was never actually verified — no firmware runs (0x1cc stays 0x3); not needed for IQ.

## ✅ RF I/Q capture working (session 2, 2026-10-17)

Bring-up without firmware (`tools/init.exe`) + LA-mode snapshot (`tools/la.exe`).
Register sequence for 11AC-series LA mode (from open-source phydm `phydm_adc_sampling.c`):

| step | register | value |
|---|---|---|
| BB enable (else BB writes are lost) | `0x02` | `|= 0x03` (+ RF `0x1f |= 7`, `0xec |= 7<<24`) |
| trigger time | `0x7cc[20:18]`, `0x7c0[14:8]` | unit, n µs |
| dbg header / rpt | `0x8f8[25:22]`, `0x8b4[7]` | 0, 1 |
| DMA type / rate / edge | `0x95c[11:8]`, `[7:5]`, `[31]` | 0 (path A IQ), 1 (40 MHz), 0 |
| BB dbg port / trig bit | `0x8fc`, `0x95c[4:0]` | 0, 3 |
| arm | `0x7c0` | byte0=0; bit0; bit3 (MAC trig); [7:6]=3 (REG); bit1 |
| fire | `0x7c0` bit5 | 1 (after arming) |
| done | `0x7c0` bit1 | → 0 |
| report | `0x7c0` | bit31 wrapped, [30:16]×8 = finish addr |
| read | `0x106=0x69`, `0x140=0x780+pg`, `0x8000..` | ring 0x20000..0x40000, 8 B/sample |

Sample format (dma 0/1): bits [11:0] = I, [23:12] = Q, 12-bit signed. 16384 samples @ 40 MS/s
(BW20) = 410 µs per snapshot. Verified on ch1/6/11/36/149: real signals, OFDM packet on ch11.
