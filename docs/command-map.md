# command-map — vendor command bus of `rtwlanu.sys` (RTL8812BU)

> Stage 1 deliverable. Restored in Ghidra 12.1.4 from `artifacts/rtwlanu.sys`
> (program `rtwlanu.sys`, base `0x140000000`). All addresses are RVA-less absolute
> (image) addresses. Cross-refs in `docs/ghidra_xrefs.txt`, decompiles in
> `docs/decompiled.txt`, `docs/key_functions.txt`, `docs/phydm_dispatch.txt`.

## 0. TL;DR — how the IQ dump is triggered

There are **two independent command tables** in the driver, both reached from one
NDIS dispatcher:

1. **35-entry "debug command" jump table** (`r4/w4/dm/uidbg/...`) — the classic
   Realtek `rtwpriv`-style register/memory console.
2. **49-entry "phydm" string table** (`lamode/dumpreg/psd/dbgport/...`) — the
   HALMAC/PHYDM diagnostic console. **`lamode` (index 0x11) is the IQ/LA dump.**

The IQ-capture command (the "gold" for SDR) is:

```
Echo lamode 1 <mode> <dbg_bit> <bbmac> <mac_ref> <dma> <trigtime>
              <head/refmask> <dbg_port> <trig_edge> <smp_rate> <cap_num>
```

- `smp_rate`: `0=80M, 1=40M, 2=20M` (the sample clock)
- `cap_num`: number of 8-byte samples to capture
- `dbg_port`: which debug port (analog/BB tap) routes the samples to the MAC RX path
- `trig_edge`: `0=positive, 1=negative`; `mode`: `0=BB,1=BB_MAC,2=RF0,3=RF1,4=MAC`

This matches the strings already catalogued in `docs/strings.md`
(`Set MAC IQ dump`, `dbg_port`, `smp_rate`, `Cap_num`, `Trig_Edge`, `Trig_Sel`,
`Dma_type`, `LA Dump finished/more`).

---

## 1. Transport / entry path

```
NDIS / vendor IOCTL  ("Process commands: %d, Unknown ioctl: %d")
   └─> command is queued into a global struct  DAT_1408e5b40
        └─> FUN_14017b0e0  (work-item / callback)   @ 0x14017b0e0
             └─> FUN_14017e690  (COMMAND DISPATCHER) @ 0x14017e690
```

`FUN_14017e690(param_1=adapter, param_2=cmd_struct)` is the single choke point.
`param_2` layout (from decompile):

| offset | field |
|--------|-------|
| `+0x08` | `cmd_index` (iVar3) — used only by the jump-table path |
| `+0x0c` | `arg_count` (iVar2) |
| `+0x10` | command keyword string (compared against magic / keyword) |
| `+0x20` | (phydm path) the phydm keyword + arguments |

The dispatcher does a `memcmp` on `param_2+0x10` and branches:

| test (memcmp at `param_2+0x10`) | magic bytes | route |
|---|---|---|
| `== "Echo"` (4 B) | `65 63 68 6f` | **phydm dispatcher** `FUN_140386620` |
| `== "fw"` (2 B) | `66 77` | `FUN_14017f96c` (fw control) |
| otherwise | — | **35-entry jump table** (cmd_index < 0x23) |

`"Incorrect cmd index"` (log @ `FUN_1401803b4`) is printed when `cmd_index >= 0x23`
in the jump-table path.

> The exact NDIS OID / IOCTL code that delivers the string to `DAT_1408e5b40` is
> still to be pinned down (Stage 2). The command *dispatch* itself is fully mapped
> below; only the user-mode→kernel door (OID/IOCTL + buffer) is open.

---

## 2. Jump table #1 — 35 "debug" commands (classic Realtek console)

- Table: **`0x1404dc650`**, array of **40-byte structs** (stride 0x28 = 5×8, the
  `* 5` scale seen in the decompile).
- Valid index: `cmd_index < 0x23` (35 entries). **User-facing number = index + 1.**
- Each struct: `+0x00 handler | +0x08 data | +0x10 name_str | +0x18 idxtag_str | +0x20 flag`
- Handler signature: `handler(adapter, cmd_index, arg_count, arg_array)`.

| # | keyword | handler | purpose |
|---|---------|---------|---------|
| 1 | `r4` | `FUN_140175050` | read memory by Double Word `<Addr> (Size)` |
| 2 | `w4` | `FUN_140175300` | write memory by Dword `<Addr> <Data> (Size)` |
| 3 | `rada` | `FUN_140175450` | read NIC adapter |
| 4 | `wada` | `FUN_1401758c0` | write NIC adapter |
| 5 | `rfd` | `FUN_140175ba0` | dump all RF registers `<RF-Path> (Addr_Start) (Addr_End)` |
| 6 | `uidbg` | `FUN_140175bc0` | utility debug (RF reg readout, `Type 0x01..`) |
| 7 | `dbgf` | `FUN_140175f90` | change debug print flag `[Flag_Type][dbg_type][dbg_comp_bit]` |
| 8 | `rfm` | `FUN_140177270` | modify dedicated RF register `<RF-Path> <Addr> <End> <Data>` |
| 9 | `r2` | `FUN_140175e80` | read memory by word |
| 10 | `w2` | `FUN_1401754f0` | write memory by word |
| 11 | `r1` | `FUN_140175640` | read memory by char |
| 12 | `w1` | `FUN_1401756e0` | write memory by char |
| 13 | `dm` | `FUN_140175820` | **Dynamic Control** `<Type> <Arg1> (Arg...)` → loops `FUN_1400426c0(adapter,Type,Arg)` |
| 14 | (dummy) | `FUN_1401773e0` | dynamic dummy (future) |
| 15 | `txrahis` | `FUN_14013bcb0` | display TX rate history |
| 16 | `fwctrl` | `FUN_1401775a0` | FW control |
| 17 | `ForTFI` | `FUN_14013bcb0` | force TxFwInfo |
| 18 | `fwdm` | `FUN_140177980` | FW dynamic control |
| 19 | `rxPathSel` | `FUN_140177530` | RX path selection control |
| 20 | `vera` | `FUN_1401779b0` | verification console |
| 21 | `verify` | `FUN_1401779d0` | dbg cmd for 92SE verify |
| 22 | `fwbuf` | `FUN_14013bcb0` | dump FW buffer (TX/RX) |
| 23 | `sec` | `FUN_14013bcb0` | security control console |
| 24 | `regdump` | `FUN_14013bcb0` | dump 92S register |
| 25 | `reset` | `FUN_14013bcb0` | reset 92S HW block |
| 26 | `eepr` | `FUN_14013bcb0` | EEPROM R/W |
| 27 | (efuse) | `FUN_14013bcb0` | EFUSE R/W/update (real/shadow) |
| 28 | (shadow) | `FUN_140177ae0` | shadow operate for RF/BB/MAC (`0/1/2/3/4/5 or m/b/r`) |
| 29 | (dbg ctrl) | `FUN_140178290` | debug control operation |
| 30 | (usb phy r) | `FUN_140179910` | USB PHY read 1 byte `Arg1=Start Arg2=Len` |
| 31 | (usb phy w) | `FUN_14017a4e0` | USB PHY write 1 byte `Arg1=Start Arg2=Value` |
| 32 | (txrx) | `FUN_14017a650` | TX/RX shortcut `Arg1=TX/RX/Counter Arg2=En/Dis` |
| 33 | `BatchIndicate` | `FUN_14017a720` | batch indicate |
| 34 | (error) | `FUN_14017a7b0` | error cmd string |
| 35 | `help` | `FUN_14017add0` | dbg cmd help |

Full decoded strings: `docs/cmd_table.txt`.

---

## 3. Jump table #2 — 49 "phydm" string commands  ← IQ dump lives here

- Dispatcher: **`FUN_140386620`** @ `0x140386620` (reached when command starts
  with `"Echo"`).
- Keyword table: **`0x14066d560`**, **49 entries × 17 B** = `[16B keyword][1B index]`.
- The dispatcher matches the keyword, reads the index byte, and `switch`es to the
  handler. Index `0x00` = `-h` (help, prints the whole list).
- Handler signature: `handler(phydm_obj, cmd_string, &offset, out_buf)`.
  `phydm_obj` = `adapter+0xe70 + 0x7789c0` (the `phydm` / HALMAC context).

| idx | keyword | handler | SDR relevance |
|-----|---------|---------|---------------|
| 0x00 | `-h` | (help) | list all |
| 0x01 | `demo` | (parser demo) | dec/hex/char/str arg echo |
| 0x02 | `rf` | `FUN_1403bc130` | RF |
| 0x03 | `dig` | `FUN_1403a0c60` | digital |
| 0x04 | `ra` | `FUN_140394a70` | rate adaptation |
| 0x05 | `profile` | `FUN_140384c60` | |
| 0x06 | `antdiv` | `FUN_140391630` | antenna diversity |
| 0x07 | `pathdiv` | (no-op) | |
| 0x08 | `dbg` | `FUN_1403870d0` | debug |
| 0x09 | `mp_dbg` | `FUN_140389df0` | MP debug |
| 0x0a | `fw_dbg` | `FUN_140388db0` | FW debug |
| 0x0b | `ability` | `FUN_14037d4a0` | |
| 0x0c | `get_txagc` | `FUN_140389c70` | |
| 0x0d | `set_txagc` | `FUN_14038bfe0` | |
| 0x0e | `smtant` | (no-op) | smart antenna |
| 0x0f | `ch_bw` | `FUN_140386480` | channel / bandwidth |
| 0x10 | `trxpath` | `FUN_1403991a0` | TX/RX path |
| **0x11** | **`lamode`** | **`FUN_1403a4770`** | **★ IQ / LA dump (the SDR capture)** |
| **0x12** | **`dumpreg`** | **`FUN_1403888a0`** | **★ register dump `0:all,1:BB,2:RF,3:MAC`** |
| 0x13 | `auto_dbg` | `FUN_1403b2110` | |
| 0x14 | `dd_dbg` | `FUN_140386fc0` | |
| 0x15 | `bigjump` | `FUN_140388c10` | |
| 0x16 | `rxrate` | `FUN_14038c790` | |
| 0x17 | `nbi` | `FUN_140399d00` | narrowband interference |
| 0x18 | `csi_mask` | `FUN_140399600` | |
| 0x19 | `dfs` | `FUN_1403b1440` | |
| 0x1a | `dfs_hist` | (no-op) | |
| 0x1b | `nhm` | `FUN_1403b85c0` | |
| 0x1c | `clm` | `FUN_1403b6bd0` | |
| 0x1d | `fahm` | `FUN_1403b7ce0` | |
| 0x1e | `env_mntr` | `FUN_1403b76a0` | env monitor |
| 0x1f | `bbinfo` | `FUN_1403853c0` | BB info |
| 0x20 | `txbf` | (inline) | TX BF `0/1/2` |
| 0x21 | `h2c` | `FUN_140394580` | **H2C send** (`Phydm Send H2C_ID`) |
| 0x22 | `ext_rfe` | `FUN_140388d00` | ext RFE |
| 0x23 | `soml` | `FUN_1403ad8a0` | |
| **0x24** | **`psd`** | **`FUN_1403b9eb0`** | **★ PSD / FFT spectrum** |
| **0x25** | **`dbgport`** | **`FUN_14038af80`** | **★ debug port control** |
| 0x26 | `dis_htstf` | (inline) | HT-STF gain on/off |
| 0x27 | `cfo_trk` | `FUN_1403b6200` | CFO track |
| 0x28 | `adapt_debug` | `FUN_1403a2b00` | |
| 0x29 | `dis_dym_ant_wgt` | `FUN_140398960` | |
| 0x2a | `force_pt_state` | `FUN_1403b2f30` | |
| 0x2b | `sta_info` | `FUN_1403956e0` | |
| 0x2c | `pause` | `FUN_14037cff0` | |
| 0x2d | `evm` | `FUN_14038a880` | EVM |
| 0x2e | `dyn_txpwr` | `FUN_1403b5870` | dyn TX power |
| 0x2f | `lna_sat` | (no-op) | LNA saturation |
| 0x30 | `anapar` | (no-op) | analog parameters |

Full decoded list: `docs/phydm_cmd_names.txt`.

---

## 4. The IQ / LA dump — `lamode` (index 0x11)

Handler `FUN_1403a4770` @ `0x1403a4770`. It is a **string sub-command** parser:

| sub-cmd (1st arg) | effect |
|---|---|
| `1` | **enable** capture — parse 10 args, store to `phydm_obj`, call `FUN_1403a3800` (LA start) |
| `-1` / `0xff` | **disable** — `[LA_Stop] LA_state=%d`, "Disable LA mode" |
| `0` | set `is_la_print` flag |

Help text embedded in the handler (verbatim):

```
{En} {0:BB,1:BB_MAC,2:RF0,3:RF1,4:MAC}
{BB:dbg_port[bit],BB_MAC:0-ok/1-fail/2-cca,MAC:ref} {DMA type} {TrigTime}
{DbgPort_head/ref_mask} {dbg_port} {0:P_Edge, 1:N_Edge} {SpRate:0-80M,1-40M,2-20M} {Capture num}
```

Argument → struct field mapping (offsets in `phydm_obj`):

| arg | meaning | stored at `phydm_obj+` |
|-----|---------|------------------------|
| En | enable (1) | `0xb338` (via `FUN_1403a3800`) |
| mode | 0:BB,1:BB_MAC,2:RF0,3:RF1,4:MAC | `0xb344` (=param_5) |
| dbg_bit | BB dbg_port bit | `0xb351` (`& 7`) |
| bbmac | 0-ok/1-fail/2-cca | — |
| mac_ref | MAC ref | — |
| dma | DMA type | `0xb340` |
| trigtime | trigger time (us) | `0xb348` region |
| head/refmask | DbgPort head / mac_ref_mask | `0xb34c` |
| dbg_port | debug port | `0xb348` |
| trig_edge | 0:P_Edge,1:N_Edge | `0xb350` |
| **smp_rate** | **0:80M,1:40M,2:20M** | **`0xb351` (`& 7`)** |
| **cap_num** | **capture count** | **`0xb354`** |

Log line: `echo lamode %d %d %d %d %d %d %x %d %d %d`.

### Capture runtime flow (work-item driven)

```
lamode(En=1)  FUN_1403a4770
   └─> FUN_1403a3800  (LA start)          @ 0x1403a3800
         sets LA_state=1, queues work item
         └─> FUN_1403a38e0  (callback)    @ 0x1403a38e0
              └─> FUN_1403a3920 (poll ADC-smp state) @ 0x1403a3920
                   • polls reg 0x7C0/0x7F0 [7:0] (adc_smp_state)
                   • "LA Query OK" / "[Polling timeout]"
                   └─> FUN_1403a3c40 (readout)        @ 0x1403a3c40
                        • computes finish_addr / buf_end / smp_number (×8B)
                        • reads samples via FUN_14039b000 (reg 0x8000/0x8004)
                          into `param_1[0x1663]` buffer, 2 DWORDs per 8B step
                        • "LA Dump finished ---------->"  -> FUN_14038b320 (Release BB dbg_port)
                        • "LA Dump more ---------->"      -> FUN_1403a3800 (loop, cap_num-- )
```

`FUN_14038b320` (Release BB dbg_port) clears the dbg-port routing regs
(`0x198C`/`0x8F8` per chip mask) — i.e. disconnects the analog tap from the RX path.

`FUN_140389910` returns the current `dbg_port_value` (reg `0x2DBC`/`0xDF4`/`4000`
by chip). `FUN_14039b340`/`FUN_14039b350`/`FUN_14039b2e0`/`FUN_14039b000` are the
low-level PHY register R/W (write `reg,mask,val`, read `reg` → DWORD).

### Register map used by the IQ path (from decompile)

| reg | use |
|-----|-----|
| `0x7C0` / `0x7F0` | ADC sample state / polling bit `[7:0]` |
| `0x8000` / `0x8004` | **sample readout** (I/Q words, 8-byte stride) |
| `0x198C` | BB dbg-port enable (release on stop) |
| `0x8F8` | dbg-port routing (release on stop) |
| `0x2DBC` / `0xDF4` | dbg_port value (per chip variant) |
| `0x0C08`/`0x0E08`/`0x093C`/`0x080C`/`0x0A04`/`0x0A2C`/`0x0808`/`0x1904`/`0x0800`/`0x0850` | path-B TRX / MP-Ant config (`FUN_14039c700`) |

> **Where do the samples go?** Into a driver-internal TX-packet buffer
> (`GetTxPktBuf`, `param_1[0x1663]`), 2×DWORD (8 B) per step, then emitted as
> normal RX MPDUs over USB bulk (see `HalUsbInMpduComplete8812AUsb`,
> `TxFillCmdDesc8812AUsb`). The host sees them as received frames — this is the
> same "IQ-after-digital / diagnostic path" model as `rtl8922-sdr` PAXI.
> The exact byte packing (12/14/16-bit, I/Q interleaving) is a Stage 2/4 item.

---

## 5. Related SDR-relevant handlers (decompiled)

| function | role |
|----------|------|
| `FUN_14039c700` | `config_phydm_trx_mode` — MP/Ant test mode, path-B TRX (TX path) |
| `FUN_14038f800` | "Test the current ant for ((%d)) ms" |
| `FUN_1403888a0` | `dumpreg {0:all,1:BB,2:RF,3:MAC}` → `FUN_1403887a0`(BB+RF) / `FUN_140388420`(BB) / `FUN_140388a00`(RF) / `FUN_1403887a0`(MAC) |
| `FUN_1403238c0` / `FUN_1402c6db4` / `FUN_14033110c` | **PSD / FFT** (Pre-Scan Peak, Scan Peak Freq/Value, `psd_raw_data`, `fft_point`, `path_sel`, `RXIQC`) |
| `FUN_140394580` | `h2c` — `Phydm Send H2C_ID` |
| `FUN_1403b1720` | `phydm_rssi_monitor_h2c` |
| `FUN_14034d280` / `FUN_14035f750` / `FUN_14036cca0` | `cfg_la_mode` — **per-chip** (8814/8821/8822) LA-mode register setup |
| `FUN_14016da80` | `hal_Associate_8812AU` — 8812 HAL entry (confirms 8812 path is active) |

`cfg_la_mode` appears for 8814/8821/8822 in strings but the LA-dump code path
(`FUN_1403a3xxx`) is generic and chip-branching is by `phydm_obj+0x54` bitfield
(see the many `& 0x20000`, `& 0x14c000` tests) — so 8812 is served by the same
handlers with different register values.

---

## 6. ✅ RESOLVED (Stage 3.1) — the user-mode→kernel door

### 6.1 NDIS OID request handler

`FUN_140018700` @ `0x140018700` is the NDIS vendor-OID request handler:

```c
NTSTATUS FUN_140018700(PVOID ctx, UINT oid, UINT *inbuf, UINT inlen,
                       UINT *outlen, PVOID outbuf);
```

It switches on the 32-bit `oid` value (decompiler shows sign-extended `0xff8180xx`;
the real 32-bit OID is `0x8180xx`). The **command-bus OIDs** are:

| OID (32-bit) | handler | purpose |
|---|---|---|
| **`0x818040`** | `FUN_14017aee4` | **SEND command** (copy user buf → global, queue work-item) |
| **`0x818041`** | `FUN_14017ae28` | **READ result** (returns ~0xF0–0xD8 bytes of command output) |
| **`0x818042`** | `FUN_14017adf8` | **POLL done-flag** (`DAT_1408d0290`: 1=done; read-and-clear) |
| `0x818045` | `FUN_140170a98` | query (in[0]=sel/id, in[1]∈{1,2,4} → dec/hex/char readout) |
| `0x818046/47/49/51/53` | vtable `ctx+0xc38` (arg 0x14/0x16/0x17) | driver-internal ops |
| `0x81801c` | `ctx+0xe98` (6 B) | read a 6-byte field |
| `0x818083` | `ctx+0x61b8e5` (1 B) | read 1 byte |
| `0x8180a1/a2` | `FUN_1400f5b38` / (0x18 B out) | read 0x18-byte struct |
| `0x818780/82` | `FUN_140042b80/0x41e30` (0x1bf) | set/get a 0x7f-flag |
| `0x81684b` | `FUN_140173e8c` (1 B) | read 1 byte |
| `0x8168fa` | `ctx+0x6354bc` (4 B) | read 4 bytes |
| `0x0101cb/a2` | `FUN_1400f5b38` | (0x18 B out) |
| `0x0111c2` | `FUN_1403fc6d8` | ? |
| `0x0111c9` | `FUN_140172c40` | `in[1]>>4`, `in[0]&0xfffff` |

Unknown OID → `0x00010001` (NDIS_STATUS_UNSUCCESSFUL / `0x10001`).

### 6.2 Send-command path (OID `0x818040`)

`FUN_14017aee4(ctx, inlen, inbuf)`:
1. `inlen < 0x150` → `RtlCopyMemory(&DAT_1408e5b40, inbuf, inlen)` (else manual copy);
2. set the "command pending" flag (`DAT_1408e5b24` → `DAT_1408e5b40`);
3. `FUN_140030860(ctx+0xe70+0x775798)` → **queue a work-item**.

Work-item `FUN_14017b0e0`: `if (pending) FUN_14017e690(ctx, &DAT_1408e5b40);`
→ dispatcher runs the command, then sets `_DAT_1408d0290 = 1` (**done-flag**).

### 6.3 Command struct layout (the OID `0x818040` input buffer)

`FUN_14017e690(ctx, buf)` parses `buf`:

| offset | field | used by |
|---|---|---|
| `+0x00` | reserved (8 B) | — |
| `+0x08` | `cmd_index` (u32) | **jump-table** path (`< 0x23`) |
| `+0x0C` | `arg_count` (u32) | all paths (must be `> 1`) |
| `+0x10` | **token[0]** (16 B) — `"Echo"` / `"fw"` / jump-table keyword | dispatch |
| `+0x20` | **token[1]** (16 B) — phydm keyword (e.g. `lamode`) / jump-table arg0 | phydm dispatcher |
| `+0x30` | **token[2]** (16 B) — arg1 | phydm handler |
| `+0x40` | **token[3]** (16 B) — arg2 | phydm handler |
| `+0x50` | **token[4]** (16 B) — arg3 | phydm handler |
| `+0x20 + n·0x10` | **token[n+1]** (16 B) — argn | phydm handler |

- **Dispatch** (`FUN_14017e690`):
  - `memcmp(buf+0x10, "Echo", 4)==0` → phydm dispatcher `FUN_140386620`
    (phydm-keyword = `buf+0x20`; args = `buf+0x30,0x40,0x50,…`).
  - `memcmp(buf+0x10, "fw", 2)==0` → `FUN_14017f96c` (fw control).
  - else → **jump table**: `handler = (&PTR_FUN_1404dc650)[cmd_index·5]`,
    `handler(ctx, cmd_index, arg_count, arg_array)` where
    `arg_array[0]=&buf+0x08`, `arg_array[i]=buf+i·0x10 (i≥1)`.
- **arg_count** = number of 16-byte token slots from `+0x10` (i.e. `1 + 1 + nargs`).
  For the phydm path the dispatcher only checks `arg_count > 1`.
- Tokens are NUL-padded to 16 B. Numeric args are ASCII, parsed by
  `FUN_14017aff0` (base 10/16/char per mode).

**Worked example** — `Echo lamode 1 0 0 0 0 0 0 0 0 0` (enable LA/IQ dump, 10 args):

```
off  size  content
+0x00  8    0x0000000000000000
+0x08  4    cmd_index = 0
+0x0C  4    arg_count = 12        ; "Echo"+"lamode"+10 args
+0x10  16   "Echo\0"
+0x20  16   "lamode\0"
+0x30  16   "1\0"                 ; En
+0x40  16   "0\0"                 ; mode
+0x50  16   "0\0"                 ; dbg_bit
+0x60  16   "0\0"                 ; bbmac
+0x70  16   "0\0"                 ; mac_ref
+0x80  16   "0\0"                 ; dma
+0x90  16   "0\0"                 ; trigtime
+0xA0  16   "0\0"                 ; head/refmask
+0xB0  16   "0\0"                 ; dbg_port
+0xC0  16   "0\0"                 ; trig_edge
+0xD0  16   "0\0"                 ; smp_rate
+0xE0  16   "0\0"                 ; cap_num
```

### 6.4 Read-result path

- **Poll**: OID `0x818042` → returns 4 B = `DAT_1408d0290` (1 = command finished;
  read-and-clear). Loop until non-zero.
- **Read**: OID `0x818041` → `FUN_14017ae28` copies the command's text output
  (built via `vsnprintf` → `FUN_14017afe0` append ring) into the caller buffer;
  sets `*outlen`. Expect ≤ ~0x100 B per chunk.

### 6.5 Remaining open items

1. **Sample packing**: confirm bit width + I/Q interleave in `FUN_1403a3c40`
   readout (regs `0x8000/0x8004`, 8-byte stride) and how they map to USB MPDU bytes.
2. **IQ RX path**: LA-mode samples are emitted as RX MPDUs over USB bulk
   (`HalUsbInMpduComplete8812AUsb`) — need the user-mode RX tap (NdisM / NPF /
   filter driver) to stream them to a file.
3. **Firmware side**: H2C commands the FW runs for LA mode (optional, `docs/firmware.md`).
4. **`psd` (0x24)** full arg format — ready-made spectrum for quick validation.
