# SDR-строки драйвера `rtwlanu.sys` (RTL8812BU)

Источник: `artifacts/rtwlanu.sys` (9 893 888 байт, 2019-12-07). Полный список кандидатов —
`docs/strings_candidates.txt`. Ниже — отобранные и аннотированные.

Легенда:
- **[GOLD]** — прямая призма для SDR (сырой IQ / захват / триггер / DMA).
- **[H2C]** — язык Host→Chiplet (реальный протокол управления чипом).
- **[CMD]** — vendor-командная шина (ioctl / priv).
- **[PSD]** — встроенный спектрометр.
- **[SHADOW]** — нерегенеративное чтение регистров.

---

## A. Сырой BB/IQ-захват (ГЛАВНЫЙ ПРИЗ) [GOLD]

Строки, описывающие настройку и запуск захвата baseband-IQ с триггером/опросом/DMA:

```
1. [BB Setting] is_fake = ((%d)),  trig_mode = ((%d)), dbg_port = ((0x%x)),
   Trig_Edge = ((%d)), smp_rate = ((%d)), Trig_Sel = ((0x%x)), Dma_type = ((%d))
4. [Set MAC IQ dump] 0x%x[7:0] = ((0x%x))
a.En= ((1)),  b.mode = ((%d)), c.Trig_Sel = ((0x%x)), d.Dma_type = ((%d))
e.Trig_Time = ((%dus)), f.Dbg_head/mac_ref_mask = ((0x%x)), g.dbg_port = ((0x%x))
h.Trig_edge = ((%d)), i.smp rate = ((%d MHz)), j.Cap_num = ((%d))
{DbgPort_head/ref_mask} {dbg_port} {0:P_Edge, 1:N_Edge} {SpRate:0-80M,1-40M,2-20M} {Capture num}
{BB:dbg_port[bit],BB_MAC:0-ok/1-fail/2-cca,MAC:ref} {DMA type} {TrigTime}
[MAC_trig] ref_mask = ((0x%x)), ref_value = ((0x%x)), dbg_port = ((0x%x))
```

**Ключевые параметры захвата:**
- `dbg_port` — **debug-порт** (точка отбора сигнала в BB-цепи). Значения: `BB:dbg_port[bit]`,
  `BB_MAC`, `RF0/RF1`, `MAC:ref` → **выбор источника** (до/после OFDM, path A/B).
- `smp_rate` / `SpRate` — **80/40/20 МГц** (0-80M,1-40M,2-20M). Номинал частоты опроса.
- `trig_mode` / `Trig_Edge` (P/N edge) / `Trig_Time` (us) / `Trig_Sel` — **триггер**.
- `Dma_type` — тип DMA (куда пишутся отсчёты).
- `Cap_num` / `smp_number` — число отсчётов в захвате.

**DMA-буфер и адресация (самое ценное):**
```
is_round_up = ((%d)), finish_addr=((0x%x * 8Byte)), Start_Addr = ((0x%x * 8Byte)), smp_number = ((%d))
smp_cnt = ((%d))
```
→ Отсчёты — **8-байтовые** слоты (2×I32 или 4×I16), адресация через `Start_Addr`/`finish_addr`
в **8-byte** слотах. Это формат DMA-буфера захвата.

**Жизненный цикл / управление:**
```
LA Dump finished ---------->
LA Dump more ---------->
Release BB dbg_port
Set dbg_port((0x%x)) success
Set fake dbg_port success
Set dbg_port fail!
BB Report Info
Dbg_port=((0x%x))
```
**`LA Dump`** = режим захвата (Low-Address? / LA-mode). Связано с `cfg_la_mode_*` (см. F).
`dbg_port` — это и есть точка отбора IQ.

---

## B. Встроенный PSD/FFT-спектрометр [PSD] [GOLD]

```
sw_avg_time=((%d)), hw_avg_time=((%d)), IQ=((%d)), fft=((%d)), path=((%d)), input =((%d)) ch=((%d)), noise_k=((%d))
{0} {sw_avg} {hw_avg 0:3} {1:I,2:Q,3:IQ} {fft_point: 128*(1:4)} {path_sel 0~3} {0:ADC, 1:RXIQC} {CH} {noise_k}
Point=%d, psd_raw_data = 0x%08x
Point=%d, psd_dB_data = %d
max_ret_psd_final = %d, max_ret_psd_b_final = %d
```
**Параметры FFT:**
- `fft_point: 128 * (1:4)` → **128/256/512** точек FFT.
- `path_sel 0~3` — выбор пути (A/B, вкл. 2-й приёмник).
- `input: {0:ADC, 1:RXIQC}` → **до I/Q-корректора (ADC)** или **после (RXIQC)** — сырой/обработанный.
- `IQ: {1:I, 2:Q, 3:IQ}` — какой канал выводить.
- `sw_avg / hw_avg` — программное/аппаратное усреднение PSD.
- `noise_k` — оценка шума (температура/шумовой пол).

**Результаты / пик-поиск:**
```
PSD Pre-Scan Peak Value
PSD Pre-Scan result
PSD Scan Peak Freq
PSD Scan Peak Value
PSD Start =>
PSD finished
PSD Threshold Offset
PSD para init
PSD_SET_FAIL
```

**ENV_MONITOR / RSSI-монитор (H2C):**
```
15. (( %s ))ENV_MONITOR
phydm_rssi_monitor_h2c
phydm_rssi_monitor_check
phydm_env_monitor_init
```
→ `phydm_rssi_monitor_h2c` — H2C-команда запуска монитора среды (PSD).

**Антенно-детекция через PSD (поиск пика по частоте):**
```
Ant Det PSD Value
AntDet(Registry) Num/PSD Value
xxxxxxxx AntennaDetect(), Max Value: Freq = %d.%d MHz
xxxxxxxx AntennaDetect(), Max Value: Freq = %d.0%d MHz
xxxxxxxx AntennaDetect(), wlpsd_cent_freq=%d, wlpsd_offset = %d, wlpsd_span = %d, wlpsd_sweep_count = %d
xxxxxxxxxxxxxx PSD Sweep Start!!
xxxxxxxxxxxxxx PSD Sweep Stop!!
WiFi PSD Cent-Ch/Offset/Span
```
→ PSD-модуль умеет **сканировать полосу** (`cent_freq/offset/span`) — готовый спектрометр.

---

## C. Нерегенеративное чтение регистров (shadow) [SHADOW]

```
dumpreg {0:all, 1:BB, 2:RF, 3:MAC}
shadow operate for RF / BB/ MAC shadow operation
  0/1/2/3/4/5 or m/b/r=MAC/BB/RF/PWR_R/PWR_W
  0x29 : Display MAC/PHY shadow register value
  HEX 0x1236=Read shadow modify map
  HEX 0x1237=Write shadow modify map
  HEX 0x1238=Use 8192se.map to update shadow modify table
  HEX 0x1239=Update shadow modify map to real efuse area
EFUSE operate to R/W/Update for Real or Shadow Table
```
**PHY-регистры:**
```
PHY register.
Dump USB2 PHY REG
Dump USB3 PHY REG
Dump PCIE(GEN1) PHY REG
Dump PCIE(GEN2) PHY REG
Dump 92S register
Dump %s PktBuf, Page %d
```
→ Shadow-режим читает реальные PHY/RF-регистры **без влияния на работу** — идеально для калибровки.

---

## D. MP / антенный тест (TX/RX, path-B) [GOLD]

```
MP/Ant test mode, support path-B TRX
Normal mode, not support path-B TRX
Test the current ant for (( %d )) ms again
  If you do not input type argument, we execute all TX/RX test
  Return: Is AP mode or In HCT Test
[ERR]test mode!!
HCT Test
antenna_test_scan_thread_cb
antenna_test_scan_workitem_cb
Fix Antenna at (( %s ))
```
**TX-мощность по антеннам (2.4G / 5G, Ant1/Ant2):**
```
    2.4G Antenna 1: [%d%s] dBm
    2.4G Antenna 2: [%d%s] dBm
    5G Antenna 1: [%d%s, %d%s, %d%s, %d%s] dBm
    5G Antenna 2: [%d%s, %d%s, %d%s, %d%s] dBm
  Antenna[%d] %-7s = %d
```
→ `path-B TRX` = **возможность передачи/приёма на втором пути** (H4). MP-тест даёт TX.

---

## E. Командная шина / transport [CMD] [H2C]

**Парсер команд:**
```
Process commands: %d, Unknown ioctl: %d
ZwDeviceIoControlFile          <- импорт API (vendor IOCTL через NDIS)
Incorrect cmd index
```

**Формат команд `cmd <index> <arg1> <arg2> <arg3>` (примеры из драйвера):**
```
cmd 13 0 0 40  --> DIG high threshold = 40
cmd 13 0 1 30  --> DIG low threshold = 30
cmd 13 0 10 60 --> DIG Max rx gain = 0x3c(60)
cmd 13 0 20 80 --> DIG enable
cmd 13 0 4 0/1 --> DIG DbgMode = 0/1
cmd 13 1 0 50  --> High threshold = 50~55
cmd 13 1 1 10  --> Low threshold 40M = 10~15
cmd 13 1 4 01234567 --> RA change high RATA0 value
cmd 13 2 0 0  --> Enter IPS
cmd 13 3 0 0/1 --> monitor reg0xc38 / disable monitor reg0xc38
cmd 13 4 0 0/1 --> disable/enable CTSToSelf
cmd 13 9 0  => Apply Vmatrix.
cmd 16 0 0   --> don't force TxSC
cmd 16 1 2   --> force TxSC value = 2
  9/11/13: 1=Start to capture
```
**`9/11/13: 1=Start to capture`** — команды index **9/11/13** со значением 1 **запускают захват**.
Это и есть «старт IQ-захвата».

**dbgcmd (расширенные):**
```
dbgcmd 19 0 0 0/1 --> RxPathSelection disable/enable
dbgcmd 19 0 1 0/1 --> DbgMode OFF/ON
dbgcmd 19 0 10/11/12/13 50 --> set rssi A/B/C/D = 50
dbgcmd 19 0 20 0/1 --> testRSSI disable/enable
dbgcmd 19 0 21 20 --> set TestRSSITh = 20
```

**Cmd Index → функция (явная таблица):**
```
Dump all RF register.          --> Cmd Index=5
Modify dedicated RF register   --> Cmd Index=8
Write memory address by word   --> Cmd Index=10
 0x0F : TX FW INFO
FW Control                     --> Cmd_Idx = 16
Force TxFwInfo                 --> Cmd_Idx = 17
FW Dynamic Control             --> Cmd_Idx = 18
```

---

## F. H2C (Host→Chiplet) — реальный протокол [H2C]

```
Phydm Send H2C_ID (( 0x%x))
[H2C]  h2c_id=((0x%x))
[H2C] FW_debug_en: (( %d )), mode: (( %d )), macid: (( %d ))
[FW][H2C=0x49]  fw_trace_en=((%d)), mode =((%d)), macid=((%d))
PHYDM h2c[0x40]=0x%x %x %x %x %x %x %x
PHYDM h2c[0x42]=0x%x %x %x %x %x %x %x
PHYDM h2c[0x4d]=0x%x %x %x %x %x %x %x
h2c[0x46]=0x%x %x %x %x %x %x %x
ccx_h2c %bx,%bx Period=%wx
phydm_clm_h2c
phydm_ra_h2c
phydm_rssi_monitor_h2c
```
**ID H2C-команд (частично раскрыты):**
- `0x40`, `0x42`, `0x4d`, `0x46` — PHYDM (RA, CLM, ...).
- `0x49` — fw trace/debug.
- `0x60/0x61/0x69` — BT coex.
- `phydm_rssi_monitor_h2c` — PSD/RSSI-монитор (см. B).

**Жизнь H2C-очереди (отладочные сообщения):**
```
[ERR]dump h2cq!!
[ERR]get h2c free space error!
[ERR]h2c ack cmd id!!
[ERR]h2c free space!!
[ERR]h2cq compare!!
[ERR]init h2cq!
[ERR]send h2c / send h2c pkt / send h2c pkt fail!!
[TRACE]h2c fs : %d
[TRACE]send h2c-buf full
h2c TimeOut / h2c busy / h2c stopped / not allow h2c
  %-35s = H2c(%d)/Ack(%d)
  H2C / C2H content
  H2C statistics
  Coex h2c/c2h overall statistics
```
→ H2C — **очередь команд** с ACK. IQ-захват управляется H2C-командами + DMA (H5 — верна).

---

## G. Прошивка и файлы [GOLD]

**Фирменные бинарники 8812 (наши):**
```
rtl8812AU\rtl8812Ufw.bin      <- основная FW
rtl8812AU\rtl8812UfwBT.bin    <- FW BT
rtl8812AU\rtl8812Ufwww.bin    <- FW WoW
rtl8812AU\AGC_TAB.txt
rtl8812AU\MAC_REG.txt
rtl8812AU\PHY_REG.txt
rtl8812AU\PHY_REG_PG.txt
rtl8812AU\RadioA.txt
rtl8812AU\RadioB.txt
rtl8812AU\TXPWR_LMT.txt
rtl8812AU\TxPowerTrack.txt
```
> Эти `.txt`-таблицы (AGC, MAC_REG, PHY_REG, Radio, TXPWR) — **MP/калибровочные данные**.
> Надо достать их из пакета Realtek / linux-firmware (`rtw88/rtl8812a_fw.bin` — бинарник).

**Загрузка FW:**
```
IO_LOAD_FW
ROM_LOAD_FW
Firmware for NIC / Firmware for NIC BT / Firmware for WoW / Firmware for Flash
FWIQK start / FWIQK start v18!!!
IQK All/ OK/ Fail/AutoLoad/FWDL
FW-IQK
Adapter->bFWReady
[TRACE]Dlfw OK, enable CPU
```

**IQK (I/Q-калибровка) — важно для SDR:**
```
FWIQK start
IQKFWOffload
iqgen setting fail:8814A
start_iqk_win8821c
start_iqk_win8822b
```
→ IQK = калибровка I/Q-баланса. Для сырого SDR-захвата важно (I/Q-imbalance, DC-offset).

---

## H. Частота / канал / ширина [GOLD]

```
{en} {CH} {pr_ch_idx 1/2/3/4/9/10} {0:20M,1:40M,2:80M}
CHANNEL_WIDTH_20 / _40 / _80 / _160 / _80_80 / _MAX
smp_number = ((%d))
smp_cnt = ((%d))
  Channel Plan ID (%d)
PHY_MapChannelPlan
Channel plan
```
→ Настраиваемые: **канал (CH)**, **ширина (20/40/80/160M)**, **прим. канал (pr_ch_idx)**.
`SpRate:0-80M,1-40M,2-20M` — это **частота опроса IQ** (не ширина канала!).

---

## I. HAL 8812A — активные функции (подтверждает 8812) [GOLD]

Подтверждают, что **8812A/8812BU активны** в этом драйвере (H1/риск «только 8814/8822» снимается):

```
RTL8812A
Realtek RTL8812AU Wireless LAN USB NIC
hal_Associate_8812AU
TxFillCmdDesc8812AUsb
HalUsbInMpduComplete8812AUsb
HalUsbInInterruptComplete8812AUsb
_NoRxAggregation_8812A
_ParsingRxAggregation_8812A
_ProcessSegregatedPackets_8812A
_QueryPacket_8812A
_SegregateDmaAggPackets_8812A
SetFwRsvdPagePkt_8812A
RtCheckForHangWorkItemCallback8812
hal_txbf_jaguar_*_8812a          (Jaguar = кодовое имя 8812)
odm_read_and_config_mp_8812a_agc_tab
odm_read_and_config_mp_8812a_mac_reg
odm_read_and_config_mp_8812a_phy_reg
odm_read_and_config_mp_8812a_phy_reg_mp
odm_read_and_config_mp_8812a_phy_reg_pg
odm_read_and_config_mp_8812a_phy_reg_pg_asus
odm_read_and_config_mp_8812a_phy_reg_pg_nec
odm_read_and_config_mp_8812a_radioa
odm_read_and_config_mp_8812a_radiob
odm_read_and_config_mp_8812a_txpwr_lmt
odm_trx_hw_ant_div_init_8812a
```
**`odm_read_and_config_mp_8812a_phy_reg_pg_asus`** — **ASUS-специфичная** калибровка PHY!
Подтверждает, что драйвер именно под ASUS USB-AC54.

**LA-режим (захват) — по чипам:**
```
cfg_la_mode_win8814b_v1
cfg_la_mode_win8821c
cfg_la_mode_win8822b
```
> ⚠️ `cfg_la_mode` виден для 8814b/8821c/8822b, **но НЕ явно для 8812a** в стрингах.
> Нужно в Ghidra проверить, активен ли LA-захват именно для 8812A (риск из плана §6).

---

## Ключевые выводы Этапа 0

1. **IQ-захват существует** — `dbg_port` + `smp_rate` (20/40/80M) + `Cap_num` +
   `Start_Addr/finish_addr (8Byte)` + `LA Dump`. Это «настоящий» diagnostic IQ (H1/H2).
2. **PSD/FFT-спектрометр встроен** — `fft_point 128/256/512`, `input ADC/RXIQC`,
   `path_sel 0~3`. Готов для быстрой валидации.
3. **Командная шина = `cmd <idx> <a1> <a2> <a3>`** через `ZwDeviceIoControlFile` (NDIS IOCTL).
   **`cmd 9/11/13 = 1 → Start to capture`** — старт захвата.
4. **H2C-протокол** — очередь команд с ACK; IQ-захват управляется H2C + DMA.
5. **8812A активен** в драйвере (подтверждено HAL-функциями), ASUS-калибровка есть.
6. **USB2 (480 Мбит/с)** — потолок ~10 MS/s @16bit. Критичный лимит для SDR.
7. **ОТКРЫТО:** активен ли `cfg_la_mode` для 8812A (виден только для 8814/8821/8822 в стрингах).
