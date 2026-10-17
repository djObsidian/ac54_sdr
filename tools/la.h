/* la.h — RTL8822B LA-mode (ADC sampling) snapshot capture, ported from open-source phydm
 * (phydm_adc_sampling.c, 11AC-series branch). Requires the chip to be brought up (rtwinit.h).
 */
#pragma once
#include "rtw.h"

#define LA_START 0x20000u   /* 8822B half-buffer mode: TXFF 0x20000..0x40000 */
#define LA_END   0x40000u
#define LA_SIZE  (LA_END - LA_START)
#define LA_NSMP  (LA_SIZE / 8)

struct la_cfg {
    int mode;        /* 0: ADC_BB_TRIG, 1: ADC_MAC_TRIG */
    int sel;         /* MAC: 0 CRCOK 1 CRCFAIL 2 CCA 3 REG(manual) */
    int dma;         /* 0x95c[11:8]; 0 = path A 12+12 bit IQ, 1 = path B */
    int rate;        /* 0x95c[7:5]; f = 80 >> rate MHz */
    uint32_t dbg, hdr;
    uint32_t ttime;  /* post-trigger time, us */
    int pre_ms;      /* pre-trigger fill time */
};
static const struct la_cfg LA_DEFAULT = { 1, 3, 0, 1, 0, 0, 100, 5 };

static int la_blk_rd(uint16_t a, void *b, uint16_t n) { return libusb_control_transfer(g_h, 0xC0, 0x05, a, 0, b, n, 2000); }
static int la_blk_wr(uint16_t a, const void *b, uint16_t n) { return libusb_control_transfer(g_h, 0x40, 0x05, a, 0, (unsigned char *)b, n, 2000); }

/* TXFF access: 0x106=0x69, 0x140[15:0]=0x780+page, window 0x8000..0x8FFF */
static int txff_xfer(uint32_t addr, uint8_t *p, uint32_t len, int write)
{
    struct libusb_device_descriptor dd;
    if (libusb_get_device_descriptor(libusb_get_device(g_h), &dd)) return -1;
    /* RTL8822BU SuperSpeed vendor reads STALL at 512 B; use short packets.
     * High Speed accepts the original 4 KB transfers. */
    uint32_t limit = dd.bcdUSB >= 0x300 ? 504 : 4096;
    uint32_t page = ~0u;
    if (wr8(0x106, 0x69) != 1) return -1;
    while (len) {
        uint32_t off = addr & 0xfff, n = 0x1000 - off;
        if (n > len) n = len;
        if (n > limit) n = limit;
        if (page != (addr >> 12)) {
            page = addr >> 12;
            if (wr16(0x140, 0x780 + page) != 2) return -1;
        }
        int r = write ? la_blk_wr((uint16_t)(0x8000 + off), p, (uint16_t)n) : la_blk_rd((uint16_t)(0x8000 + off), p, (uint16_t)n);
        if (r != (int)n) return -1;
        addr += n; p += n; len -= n;
    }
    return 0;
}

/* Run one capture. out[] receives up to LA_NSMP 64-bit samples in time order.
 * Returns number of samples (0 on trigger timeout, <0 on USB error). */
static int la_capture(const struct la_cfg *c, uint64_t *out, int verbose)
{
    static uint8_t raw[LA_SIZE];
    wr8(0x7c0, 0);                                 /* stop previous */

    uint32_t unit = 0;
    while (unit < 6 && (c->ttime >> unit) >= 128) unit++;
    wmask(0x7cc, 0x1c0000, unit);
    wmask(0x7c0, 0x7f00, (c->ttime >> unit) & 0x7f);

    wmask(0x8f8, 0x3c00000, c->hdr);
    wmask(0x8b4, 1u << 7, 1);
    wmask(0x95c, 0xf00, c->dma);
    wmask(0x95c, 1u << 31, 0);
    wmask(0x95c, 0xe0, c->rate);
    wr32(0x8fc, c->dbg);
    wmask(0x95c, 0x1f, c->sel);

    uint8_t v = 1;                                 /* enable */
    if (c->mode == 1) v |= (1u << 3) | ((c->sel & 3) << 6);
    wr8(0x7c0, v);
    wr8(0x7c0, v | 2);                             /* arm (polling bit) */
    if (c->pre_ms) Sleep(c->pre_ms);
    if (c->mode == 1 && c->sel == 3) wr8(0x7c0, v | 2 | (1u << 5));   /* manual trigger */

    int ok = 0;
    for (int i = 0; i < 200; i++) { if (!(rd8(0x7c0) & 2)) { ok = 1; break; } Sleep(1); }
    if (!ok) { if (verbose) printf("LA poll timeout (0x7c0=0x%08x)\n", rd32(0x7c0)); wr8(0x7c0, 0); return 0; }

    wr8(0x106, 0x69);
    uint32_t r = rd32(0x7c0);
    int round = (r >> 31) & 1; uint32_t fin = ((r & 0x7fff0000) >> 16) << 3;
    if (txff_xfer(LA_START, raw, LA_SIZE, 0)) { wr8(0x7c0, 0); return -1; }
    wr8(0x7c0, 0);

    uint32_t start = LA_START, n;
    if (fin < LA_START || fin >= LA_END) fin = LA_START;
    if (round) { start = fin + 16; while (start >= LA_END) start -= LA_SIZE; n = LA_NSMP; }
    else n = (fin - LA_START) / 8;
    for (uint32_t k = 0, a = start; k < n; k++) {
        memcpy(&out[k], raw + (a - LA_START), 8);
        a += 8; if (a >= LA_END) a = LA_START;
    }
    if (verbose) printf("LA: 0x7c0=0x%08x round=%d finish=0x%05x -> %u samples\n", r, round, fin, n);
    return (int)n;
}

/* 12-bit I [11:0], 12-bit Q [23:12] (dma 0/1) */
static inline int la_i(uint64_t s) { int v = (int)(s & 0xfff); return v >= 0x800 ? v - 0x1000 : v; }
static inline int la_q(uint64_t s) { int v = (int)((s >> 12) & 0xfff); return v >= 0x800 ? v - 0x1000 : v; }
