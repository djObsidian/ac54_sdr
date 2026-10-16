/* la.exe — RTL8822B LA-mode (ADC sampling) capture, ported 1:1 from open-source phydm
 * (morrownr/88x2bu-20210702 hal/phydm/phydm_adc_sampling.c, 11AC-series branch).
 *
 *   la [key=val ...]
 *     mode=1     0:ADC_BB_TRIG 1:ADC_MAC_TRIG (default 1 + sel=3 = manual REG trigger)
 *     sel=3      ADC_MAC: 0 CRCOK 1 CRCFAIL 2 CCA 3 REG(manual) ; ADC_BB: dbg_port bit index
 *     dma=0      0x95c[11:8] DMA type (what is stored per 64-bit sample)
 *     rate=0     0x95c[7:5]  f_smp = 80 >> rate MHz
 *     dbg=0      BB dbg port (0x8fc)
 *     hdr=0      dbg port header (0x8f8[25:22])
 *     ttime=100  trigger time in us (post-trigger capture length)
 *     fill=1     pre-fill LA buffer with 0xA5A5A5A5 to see what HW wrote
 *     out=la.bin dump raw 64-bit samples (h:l as stored) of whole ring buffer, in time order
 */
#include "rtw.h"
#include <stdlib.h>

#define LA_START 0x20000u   /* 8822B half-buffer mode: TXFF 0x20000..0x40000 */
#define LA_END   0x40000u
#define LA_SIZE  (LA_END - LA_START)

static int blk_rd(uint16_t a, void *buf, uint16_t len)
{ return libusb_control_transfer(g_h, 0xC0, 0x05, a, 0, buf, len, 2000); }
static int blk_wr(uint16_t a, const void *buf, uint16_t len)
{ return libusb_control_transfer(g_h, 0x40, 0x05, a, 0, (unsigned char *)buf, len, 2000); }

/* TXFF access: 0x106=0x69, 0x140[15:0]=0x780+page, window 0x8000..0x8FFF (4 KB page) */
static void txff_page(uint32_t pg) { wmask(0x140, 0xffff, 0x780 + pg); }

static int txff_read(uint32_t addr, uint8_t *dst, uint32_t len)
{
    while (len) {
        uint32_t pg = addr >> 12, off = addr & 0xfff, n = 0x1000 - off;
        if (n > len) n = len;
        txff_page(pg);
        if (blk_rd((uint16_t)(0x8000 + off), dst, (uint16_t)n) != (int)n) return -1;
        addr += n; dst += n; len -= n;
    }
    return 0;
}
static int txff_write(uint32_t addr, const uint8_t *src, uint32_t len)
{
    while (len) {
        uint32_t pg = addr >> 12, off = addr & 0xfff, n = 0x1000 - off;
        if (n > len) n = len;
        txff_page(pg);
        if (blk_wr((uint16_t)(0x8000 + off), src, (uint16_t)n) != (int)n) return -1;
        addr += n; src += n; len -= n;
    }
    return 0;
}

static long opt(int argc, char **argv, const char *k, long def)
{
    size_t kl = strlen(k);
    for (int i = 1; i < argc; i++)
        if (!strncmp(argv[i], k, kl) && argv[i][kl] == '=') return strtol(argv[i] + kl + 1, NULL, 0);
    return def;
}
static const char *opts(int argc, char **argv, const char *k, const char *def)
{
    size_t kl = strlen(k);
    for (int i = 1; i < argc; i++)
        if (!strncmp(argv[i], k, kl) && argv[i][kl] == '=') return argv[i] + kl + 1;
    return def;
}

int main(int argc, char **argv)
{
    int mode = (int)opt(argc, argv, "mode", 1), sel = (int)opt(argc, argv, "sel", 3);
    int dma = (int)opt(argc, argv, "dma", 0), rate = (int)opt(argc, argv, "rate", 0);
    uint32_t dbg = (uint32_t)opt(argc, argv, "dbg", 0), hdr = (uint32_t)opt(argc, argv, "hdr", 0);
    uint32_t ttime = (uint32_t)opt(argc, argv, "ttime", 100);
    int fill = (int)opt(argc, argv, "fill", 1);
    const char *out = opts(argc, argv, "out", "la.bin");

    setvbuf(stdout, NULL, _IONBF, 0);
    if (rtw_open()) return 1;
    static uint8_t buf[LA_SIZE];

    wr8(0x7c0, 0);                 /* stop any running LA capture first */
    wr8(0x106, 0x69);
    if (fill) {
        memset(buf, 0xA5, sizeof buf);
        DWORD t0 = GetTickCount();
        if (txff_write(LA_START, buf, LA_SIZE)) { fprintf(stderr, "fill failed\n"); return 1; }
        memset(buf, 0, sizeof buf);
        txff_read(LA_START, buf, LA_SIZE);
        int bad = 0; for (uint32_t i = 0; i < LA_SIZE; i++) if (buf[i] != 0xA5) bad++;
        printf("prefill 0x%x..0x%x with A5: %u ms, verify mismatches=%d\n", LA_START, LA_END, GetTickCount() - t0, bad);
    }

    /* 2. trigger time: 0x7cc[20:18]=unit, 0x7c0[14:8]=num */
    uint32_t unit = 0;
    while (unit < 6 && (ttime >> unit) >= 128) unit++;
    wmask(0x7cc, 0x1c0000, unit);
    wmask(0x7c0, 0x7f00, (ttime >> unit) & 0x7f);

    /* 3. BB setting (11AC series) */
    wmask(0x8f8, 0x3c00000, hdr);   /* dbg port header sel */
    wmask(0x8b4, 1u << 7, 1);       /* update rpt every pkt */
    wmask(0x95c, 0xf00, dma);
    wmask(0x95c, 1u << 31, 0);      /* posedge */
    wmask(0x95c, 0xe0, rate);

    /* 4. BB dbg port */
    wr32(0x8fc, dbg);
    wmask(0x95c, 0x1f, mode == 4 ? 0 : sel);

    /* 5. MAC IQ dump */
    wr8(0x7c0, 0);
    wmask(0x7c0, 1u << 0, 1);              /* enable LA HW block */
    if (mode == 1) {
        wmask(0x7c0, 1u << 3, 1);          /* trig source = MAC */
        wmask(0x7c0, 0xc0, sel);
    } else {
        wmask(0x7c0, 1u << 3, 0);
    }
    wmask(0x7c0, 1u << 1, 1);              /* polling bit for BB ADC mode (arms) */
    Sleep((DWORD)opt(argc, argv, "pre", 20));  /* let ring buffer fill with pre-trigger data */
    if (mode == 1 && sel == 3) wmask(0x7c0, 1u << 5, 1);   /* manual trigger 0->1 */
    printf("set: 0x7c0=0x%08x 0x7cc=0x%08x 0x95c=0x%08x 0x8fc=0x%08x 0x8f8=0x%08x\n",
           rd32(0x7c0), rd32(0x7cc), rd32(0x95c), rd32(0x8fc), rd32(0x8f8));

    /* 6. poll bit1 clear (max 2 s) */
    int ok = 0; uint32_t v = 0;
    for (int i = 0; i < 20; i++) {
        v = rd8(0x7c0);
        if (!(v & 2)) { ok = 1; break; }
        Sleep(100);
    }
    printf("poll: 0x7c0[7:0]=0x%02x -> %s\n", v, ok ? "DONE" : "TIMEOUT");

    /* 7. report */
    wr8(0x106, 0x69);
    uint32_t r = rd32(0x7c0);
    int round = (r >> 31) & 1; uint32_t fin = ((r & 0x7fff0000) >> 16) << 3;
    printf("report 0x7c0=0x%08x round_up=%d finish_addr=0x%05x\n", r, round, fin);

    /* 8. read whole buffer, rotate into time order */
    txff_read(LA_START, buf, LA_SIZE);
    uint32_t a5 = 0; for (uint32_t i = 0; i < LA_SIZE; i += 8) { uint64_t q; memcpy(&q, buf + i, 8); if (q == 0xA5A5A5A5A5A5A5A5ull) a5++; }
    printf("samples still A5-filled: %u / %u\n", a5, LA_SIZE / 8);

    uint32_t start = LA_START, nsmp;
    if (fin < LA_START || fin >= LA_END) printf("WARN finish_addr outside LA window (rel? 0x%x)\n", fin);
    if (round) {
        start = fin + 16;
        while (start >= LA_END) start -= LA_SIZE;
        if (start < LA_START) start = LA_START + ((start - 0) % LA_SIZE);
        nsmp = LA_SIZE / 8;
    }
    else nsmp = (fin > LA_START && fin <= LA_END) ? (fin - LA_START) / 8 : LA_SIZE / 8;
    FILE *f = fopen(out, "wb");
    for (uint32_t k = 0, a = start; k < nsmp; k++) {
        fwrite(buf + (a - LA_START), 8, 1, f);
        a += 8; if (a >= LA_END) a = LA_START;
    }
    fclose(f);
    printf("wrote %u samples (8 B each, [l32][h32]) to %s\n", nsmp, out);
    for (uint32_t k = 0, a = start; k < 16 && k < nsmp; k++, a += 8) {
        uint32_t l, h; memcpy(&l, buf + (a - LA_START), 4); memcpy(&h, buf + (a - LA_START) + 4, 4);
        printf("  [%2u] %08x%08x\n", k, h, l);
    }
    wmask(0x7c0, 1u << 0, 0);   /* disable LA block */
    rtw_close();
    return 0;
}
