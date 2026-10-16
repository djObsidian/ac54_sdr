# PLAN — скрытые SDR-возможности ASUS USB-AC54 (Realtek RTL8812BU)

Цель: найти и вывести наружу **сырые IQ-отсчёты** (raw baseband I/Q) из WiFi-чипа
RTL8812BU, чтобы использовать адаптер как SDR (приёмник, спектр, и в перспективе — TX),
по методологии [djObsidian/wifi-sdr](https://github.com/djObsidian/wifi-sdr).

Статус: **Этап 0 + Этап 1 завершены. Модель подтверждена: чисто хост-драйвер
(как `rtl8922-sdr`) — кастомная прошивка НЕ нужна.** Следующий шаг — **Этап 3 (хост-тул)**.
Этап 2 (прошивка) → опциональная валидация, не prereq.
Карта команд: `docs/command-map.md`. IQ-dump = `Echo lamode ...` (phydm idx 0x11) —
конфиг MAC-регистров + readout `0x8000/0x8004` + USB RX-MPDU; H2C/прошивка в data-path не участвует.

---

## 0. Что у нас есть (факты)

| Параметр | Значение |
|---|---|
| Устройство | ASUS USB Wireless Adapter (USB-AC54) |
| USB ID | `VID_0B05` (ASUSTeK) / `PID_1841`, `REV_0210` |
| Чипсет | **Realtek RTL8812BU** (USB 3.0, 802.11ac 2x2, AC1733, 2.4 + 5 ГГц) |
| Драйвер (Win) | `rtwlanu.sys` (9.9 MB), INF `netrtwlanu.inf` |
| Путь драйвера | `C:\Windows\System32\DriverStore\FileRepository\netrtwlanu.inf_amd64_1815bafd14dc59f0\rtwlanu.sys` |
| Семейство | единый Realtek «rtw» стек (HAL/HALMAC), поддерживает 8812AU/BU, 8814AU, 8821C, 8822B |
| Linux-стек | **rtw88** (не rtw89!) — чипы 8812/8814/8821/8822. FW: `rtw88/rtl8812a_fw.bin` |
| Ghidra | `D:\Programs\ghidra_12.1.4_PUBLIC_20260921\ghidra_12.1.4_PUBLIC` |
| Прошивка (из стрингов) | `rtl8812AU\rtl8812Ufw.bin`, `rtl8812UfwBT.bin`, `rtl8812Ufwww.bin` (WoW) |

> Важно: чип **8812** — это **не** тот же, что в прецеденте `rtl8922-sdr` (8922AE/PCIe,
> стек rtw89). Это поколение **88xxAU/BU** (USB, стек rtw88). IQ-путь ищется заново,
> но методология переносится 1:1.

---

## 1. Прецеденты и что из них берём

| Проект | Чип | Метод | Что заимствуем |
|---|---|---|---|
| [plaingca/rtl8922-sdr](https://github.com/plaingca/rtl8922-sdr) | Realtek 8922AE (PCIe) | патч Linux rtw89 → **PATH1 diagnostic I/Q** / **PAXI**; SoapySDR-плагин; 160 MS/s | Концепция: диагностический IQ-путь в Realtek существует; «IQ after digital processing»; патч драйвера + стрим через шину; SoapySDR-обёртка |
| [luigifcruz/BirriaSDR](https://github.com/luigifcruz/BirriaSDR) | **MT7961/MT7921AU (USB-адаптер!)** | **MtkICAP** (I/Q Capture) | Самая близкая аналогия (USB-адаптер). Инструменты: [MtkICAPtool](https://github.com/MtkWifiRev/MtkICAPtool), [MtkCSIdump](https://github.com/MtkWifiRev/MtkCSIdump) |
| [ESPARGOS/esp-sdr](https://github.com/ESPARGOS/esp-sdr) | ESP32 | прошивка обходит WiFi и читает сырые IQ | Доказательство, что «скрытый SDR» — нормальный приём |
| [seemoo-lab/nexmon](https://github.com/seemoo-lab/nexmon) | Broadcom/Cypress | патчинг прошивки | Патчинг прошивки как способ вскрытия IQ |

**Общий паттерн:** WiFi-чипы имеют **недокументированный диагностический/test-режим**,
который дампает сырые baseband I/Q. Наша задача — найти его для RTL8812BU.

---

## 2. Что уже найдено В НАШЕМ ДРАЙВЕРЕ (главное, gold)

Стринг-скан `rtwlanu.sys` показал, что в нём уже есть **скрытая vendor-командная шина**
(тест / MP / HCT / «priv» интерфейс) + явные SDR-режимы. Это «иглы», которые мы будем
догонять в Ghidra:

**A. Сырой BB/IQ-захват (главный приз):**
- `Set MAC IQ dump` — прямой дамп IQ на MAC-уровне.
- `1. [BB Setting] is_fake / trig_mode / dbg_port / Trig_Edge / smp_rate / Trig_Sel / Dma_type`
- `{SpRate:0-80M,1-40M,2-20M} {Capture num}` — частота опроса **80/40/20 МГц** + число отсчётов.
- `{En} {0:BB,1:BB_MAC,2:RF0,3:RF1,4:MAC}` — выбор источника (BB / RF0 / RF1 / MAC).
- `Release BB dbg_port`, `BB Report Info`, `LA Dump finished`, `LA Dump more`.

**B. Встроенный PSD/FFT-спектрометр:**
- `sw_avg_time / hw_avg_time / IQ / fft / path / input / ch / noise_k`
- `{1:I,2:Q,3:IQ} {fft_point: 128*(1:4)} {path_sel 0~3} {0:ADC, 1:RXIQC}`
- `PSD Pre-Scan Peak Value`, `PSD Scan Peak Freq`, `Point=%d, psd_raw_data = 0x%08x`
- `ENV_MONITOR`, `phydm_rssi_monitor_h2c`.

**C. Нерегенеративное чтение регистров (shadow):**
- `dumpreg {0:all,1:BB,2:RF,3:MAC}`
- `shadow operate for RF / BB / MAC`, `m/b/r=MAC/BB/RF/PWR_R/PWR_W`
- `Dump USB2/USB3 PHY REG`, `Dump PCIE PHY REG`, `Dump 92S register`.

**D. MP/антенный тест (TX/RX, path-B):**
- `MP/Ant test mode, support path-B TRX`, `Test the current ant for ((%d)) ms`
- `HCT Test`, `[ERR]test mode!!`, `If you do not input type argument, we execute all TX/RX test`.

**E. Командная шина / transport:**
- `Process commands: %d, Unknown ioctl: %d` → **парсер команд**.
- `ZwDeviceIoControlFile` → **vendor IOCTL** (NDIS).
- `cmd 13 3 0 0/1 --> monitor reg0xc38`, `cmd index`, `9/11/13: 1=Start to capture`.
- **H2C** (Host→Chiplet): `dump h2cq`, `phydm_rssi_monitor_h2c`.
- USB RX-путь: `TxFillCmdDesc8812AUsb`, `HalUsbInMpduComplete8812AUsb`, `Dump TX/RX buffer`.

**Вывод:** драйвер уже знает, как (1) настроить BB-захват с триггером/опросом/DMA,
(2) запустить PSD/FFT, (3) читать регистры в shadow-режиме, (4) делать MP TX/RX.
Нам нужно **вскрыть, как это вызывается** (IOCTL/команды/буферы), и **собрать хост-тул**.

---

## 3. Рабочие гипотезы (проверяем по порядку)

1. **H1 — IQ-захват = BB diagnostic path** (аналог MTK ICAP / RTL8922 PAXI).
   Данные идут: АФХ → микшер → baseband I/Q → DMA → хост. Это «настоящий» SDR.
2. **H2 — «MAC IQ dump» = дамп после OFDM/до OFDM** на MAC-уровне. Менее «сыро»,
   но тоже полезно (аналог RTL8922 «IQ after digital processing»).
3. **H3 — PSD/FFT-путь** = готовый спектрометр (удобно для валидации H1/H2 и
   быстрой «картинки спектра» без полного захвата).
4. **H4 — MP/antenna test** даёт **TX** (path-B TRX) — возможность передачи.
5. **H5 — H2C-команды** = реальный «язык» между драйвером и чипом; IQ-захват
   управляется H2C-командами + DMA-буферами.

---

## 4. Инструментарий

- **Ghidra** `D:\Programs\ghidra_12.1.4_PUBLIC_20260921\ghidra_12.1.4_PUBLIC` —
  разбор `rtwlanu.sys`, прошивки, поиск командной шины.
- **Драйвер/прошивки** — из DriverStore + из пакета Realtek (категория 663) +
  `linux-firmware/rtw88/rtl8812a_fw.bin`.
- **Linux + rtw88** (`lwfinger/rtw88` backport) — самый удобный путь для вывода IQ
  (debugfs/`rtwpriv`-подобный интерфейс), аналог `rtl8922-sdr`.
- **SDR-приложения для валидации:** GQRX / SDR# / GNU Radio + `iq-capture` (WAV I/Q).
- **RF-источник для калибровки:** сигнальный генератор / второй передатчик / Pluto
  (как в `rtl8922-sdr` для TX-валидации).

---

## 5. Поэтапный план работ

### Этап 0 — Разведка и фиксация (0.5–1 день) ✅
- [x] Сохранить артефакты: `rtwlanu.sys`, `netrtwlanu.inf` в `artifacts/`.
- [x] USB-маппинг: 4 BULK + 1 INT endpoint, maxpacket 512 B, скоростной режим — `docs/usb-map.md`.
- [x] Текущий режим/режимы адаптера зафиксированы.
- **Критерий готовности:** ✅ `docs/usb-map.md` + `docs/strings.md`.

### Этап 1 — Вскрытие командной шины в Ghidra (2–4 дня) ✅
- [x] Загрузить `rtwlanu.sys` в Ghidra (x86-64, Windows PE), автоанализ (~150 MB DB).
- [x] Найти **дескриптор IOCTL / NDIS handler**: `FUN_14017b0e0` (work-item) →
      `FUN_14017e690` (диспетчер). Магии: `"Echo"` → phydm, `"fw"` → fw, иначе jump-table.
- [x] Восстановить **ОБЕ таблицы команд** (cmd → обработчик):
      - 35 «debug»-команд (`r4/w4/dm/uidbg/...`), таблица `0x1404dc650`, шаг 40 B.
      - 49 «phydm»-команд (`lamode/dumpreg/psd/dbgport/...`), таблица `0x14066d560`.
- [x] Найти обработчики: **`lamode`(IQ-dump) `FUN_1403a4770`**, `dumpreg FUN_1403888a0`,
      `psd FUN_1403b9eb0`, `dbgport FUN_14038af80`, MP/Ant `FUN_14039c700`, `cfg_la_mode`.
- [x] Формат запроса IQ-dump + runtime flow (work-item → poll → readout → USB MPDU).
- [ ] (→ Этап 2) Точный IOCTL/OID + буфер-маппинг; битность/интерливинг I/Q;
      H2C-команды в прошивке.
- **Критерий готовности:** ✅ `docs/command-map.md` + `ghidra_xrefs.txt` +
  `decompiled.txt` + `cmd_table.txt` + `phydm_cmd_names.txt` + адреса функций.

### Этап 2 — Природа IQ / валидация (ОПЦИОНАЛЬНО, не prereq)
> **Коррекция (по прецеденту `rtl8922-sdr` + наш Этап 1):** IQ-захват — **чисто
> хост-драйвер**: конфиг MAC-регистров (`lamode`) + readout `0x8000/0x8004`
> + выдача USB RX-MPDU. **Кастомную прошивать не нужно.** Прошивка лишь держит
> чип в рабочем PHY-состоянии; в data-path она не участвует (H2C не требуется).
> Этап остаётся как *опциональное* углубление.
- [ ] (опц.) Подтвердить природу IQ (сырой до OFDM vs пост-digital) — **эмпирически
      в Этапе 4** (тон → спектр), не обязательно через разбор прошивки.
- [ ] (опц.) H2C-команды / узлы генерации I/Q, если нужна глубокая модель
      (`dump h2cq`, `phydm_rssi_monitor_h2c`).
- **Критерий готовности (если делать):** краткий разбор в `docs/firmware.md`.

### Этап 3 — Реализация хост-тула (3–5 дней)  ← **СЛЕДУЮЩИЙ ШАГ**
> Единственное, чего реально не хватает (из Этапа 1): **точный NDIS OID/IOCTL +
> layout запроса**, кладущий команду в `DAT_1408e5b40` → `FUN_14017b0e0`. Это
> **первый подшаг** Этапа 3 (Ghidra: функция со строкой `Process commands: %d,
> Unknown ioctl: %d` @ `0x14040a4e1` + write-сайты глобала `DAT_1408e5b40`).
Путь A — **Windows** (устройство уже здесь):
- [ ] Написать C/C++ тул: `CreateFile` на `\Device\<ndis>`, `DeviceIoControl`
      с IOCTL из Этапа 1.
- [ ] Последовательность: инициализация → выбор канала/частоты →
      `BB Setting` (smp_rate, trig, DMA) → `Start capture` → чтение буфера →
      запись в файл (`.iq`/WAV-IQ).
- [ ] Обёртка в **SoapySDR-плагин** (как в `rtl8922-sdr`) для GQRX/SDR#.

Путь B — **Linux + rtw88** (рекомендуемый для итераций):
- [ ] Поднять 8812BU под `rtw88` (lwfinger backport), проверить `rtl8812a_fw.bin`.
- [ ] Использовать debugfs/`rtwpriv`-подобный интерфейс; при отсутствии нужных
      команд — патч rtw88 по аналогии с `rtl8922-sdr`.
- [ ] Стрим IQ через USB 3.0 → файл / Soapy.

- **Критерий готовности:** тул захватывает непрерывный поток I/Q в файл,
  который GQRX/SDR#/GNU Radio открывают и показывают спектр.

### Этап 4 — Валидация RF (2–3 дня)
- [ ] **Тест тона:** подать CW на известную частоту (генератор/Pluto), убедиться
      что пик на нужной частоте, измерить IQ-баланс, DC-offset, шумовой пол.
- [ ] **Измерить фактическую полосу пропускания** и **макс. частоту опроса**
      (реальные MS/s, а не номинал 80/40/20 МГц).
- [ ] **Определить диапазон частот** (2.4 + 5 ГГц, реально настраиваемые центры).
- [ ] **TX (если H4 верна):** сгенерировать тон/OFDM, поймать на втором приёмнике.
- **Критерий готовности:** таблица характеристик (см. Этап 5) + видео/скрин
  waterfall с реальным сигналом.

### Этап 5 — Документирование и публикация
- [ ] Заполнить **карточку чипа** по формату [wifi-sdr](https://github.com/djObsidian/wifi-sdr):
  sample rate RX (реальный), разрешение (бит), analog bandwidth, frequency range,
  output interface (USB 3.0), simultaneous IQ channels (path A/B), source.
- [ ] Написать PR/issue в `djObsidian/wifi-sdr` с RTL8812BU + ссылкой на наш тул.
- [ ] README с «быстрым стартом» и ограничениями (retune gaps, непрерывность).

---

## 6. Риски и открытые вопросы

| Риск | Митигация |
|---|---|
| IQ — «пост-digital» (H2), а не сырой RF | Валидировать полосу/диапазон в Этапе 4; если узко — фиксировать как «diagnostic IQ» (как RTL8922) |
| Команды есть в драйвере, но не для 8812BU (только 8814/8822) | Сверить с `hal_Associate_8812AU`/`8814AU`; проверить, какие HAL-функции активны для 8812 |
| Нужны DMA-буферы в user-space | Восстановить маппинг из Ghidra (Этап 1); фолбэк — Linux debugfs |
| USB 3.0 — предел пропускной способности | 80 MS/s × 16 бит × 2 (I/Q) = 320 МБ/с < 5 Гбит/с USB3 — ок, но проверим реально |
| Пропущенные отсчёты при retune | Отслеживать «retune gaps» (как в rtl8922-sdr); кодированный референс для проверки непрерывности |
| Чип в режиме WoW/питания | Перевести в active, отключить power-save, зафиксировать country |

**Ключевые вопросы (статус):**
1. ~~Какой IOCTL/команда запускает сырой IQ-захват?~~ ✅ **`Echo lamode 1 ...`** (Этап 1).
   Осталось: точный **NDIS OID/IOCTL** + layout буфера (Этап 3, подшаг 1).
2. Формат отсчётов в readout `0x8000/0x8004` (12/14/16 бит, interleaved I,Q)? — **Этап 3/4**.
3. До или после OFDM-demod данные? — **валидируем эмпирически в Этапе 4** (тон → спектр),
   не через прошивку.
4. Сколько каналов IQ одновременно (path A/B)? — Этап 4.
5. Реальный диапазон частот и полоса (MS/s)? — Этап 4.

---

## 7. Deliverables

1. `docs/strings.md` — все SDR-строки драйвера + контекст.
2. `docs/command-map.md` — cmd → IOCTL → args → out-buffer (Ghidra).
3. `docs/firmware.md` — H2C/природа IQ/частоты (ОПЦИОНАЛЬНО, см. Этап 2).
4. `tool/` — рабочий захватчик I/Q (Windows и/или Linux) + SoapySDR-плагин.
5. `artifacts/` — бинарники, прошивки, сэмплы IQ.
6. Заполненная карточка RTL8812BU + PR в `djObsidian/wifi-sdr`.

---

## 8. Следующий ход (новая сессия)

Этапы 0–1 завершены. Начинаем **Этап 3 (хост-тул)**:
1. **Ghidra:** найти точный NDIS OID/IOCTL + layout запроса — строка
   `Process commands: %d, Unknown ioctl: %d` @ `0x14040a4e1`; write-сайты глобала
   `DAT_1408e5b40`; обработчик `FUN_14017b0e0`. Заполнить §6 `docs/command-map.md`.
2. **Хост-тул (C/C++):** `CreateFile` на `\Device\<ndis>` + `DeviceIoControl`
   (найденный IOCTL) → отправить `Echo lamode 1 ...`; чтение USB RX-MPDU → `.iq`/WAV.
3. **Валидация (Этап 4):** подать CW-тон, убедиться в пике на нужной частоте,
   снять полосу/диапазон (GQRX/SDR#/GNU Radio).
