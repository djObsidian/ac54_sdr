# USB-карта ASUS USB-AC54 (RTL8812BU)

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
