/* rtwinit.h — minimal RTL8822BU bring-up over libusb, ported from Linux rtw88
 * (rtw8822b.c, mac.c, phy.c, efuse.c; GPL-2.0 OR BSD-3-Clause, Realtek).
 * No firmware download: only what the BB/RF/ADC + LA-mode need.
 *
 *   rtw_power_on()  -> pre_system_cfg + card_enable pwr-seq + system_cfg + REG_CR
 *   rtw_read_efuse()-> rfe_option, xtal cap, mac
 *   rtw_phy_init()  -> BB/RF enable, MAC/BB/AGC/RF tables, xtal, trx mode, rfe
 *   rtw_set_channel(ch, bw)
 */
#pragma once
#include "rtw.h"
#include "t8822b.h"

#define BB_PATH_A  1
#define BB_PATH_B  2
#define BB_PATH_AB 3
#define RFREG_MASK 0xfffff
enum { BW20 = 0, BW40 = 1, BW80 = 2, BW5 = 3, BW10 = 4 };

struct rtw_state {
    uint8_t cut;         /* REG_SYS_CFG1[15:12] */
    uint8_t rf_2t2r;
    uint8_t rfe_option;
    uint8_t xtal_k;
    uint8_t mac[6];
    uint8_t ant_tx, ant_rx;
    uint8_t ch, bw;
    int verbose;
};
static struct rtw_state rs = { .ant_tx = BB_PATH_A, .ant_rx = BB_PATH_AB };

static void usleep_(unsigned us) { if (us >= 1000) Sleep(us / 1000); /* USB latency covers < 1 ms */ }

static void w8_set(uint32_t a, uint8_t b) { wr8(a, rd8(a) | b); }
static void w8_clr(uint32_t a, uint8_t b) { wr8(a, rd8(a) & ~b); }
static void w32_set(uint32_t a, uint32_t b) { wr32(a, rd32(a) | b); }
static void w32_clr(uint32_t a, uint32_t b) { wr32(a, rd32(a) & ~b); }
static void w32s_mask(uint32_t a, uint32_t m, uint32_t v) { wmask(a, m, v); wmask(a + 0x200, m, v); }

/* ---------------- RF access (8822B: direct read @0x2800/0x2c00, SIPI write @0xc90/0xe90) */
static const uint32_t rf_base[2] = { 0x2800, 0x2c00 };
static const uint32_t rf_sipi[2] = { 0xc90, 0xe90 };

static uint32_t rf_read(int path, uint32_t addr, uint32_t mask)
{
    addr &= 0xff; mask &= RFREG_MASK;
    return rmask(rf_base[path] + (addr << 2), mask);
}
static void rf_write(int path, uint32_t addr, uint32_t mask, uint32_t data)
{
    addr &= 0xff; mask &= RFREG_MASK;
    if (mask != RFREG_MASK) {
        uint32_t old = rf_read(path, addr, RFREG_MASK);
        data = (old & ~mask) | ((data << ffs32(mask)) & mask);
    }
    wr32(rf_sipi[path], ((addr << 20) | (data & 0xfffff)) & 0x0fffffff);
}

/* ---------------- power sequence */
static int pwr_seq(const struct pwr_cmd *c)
{
    uint8_t cutmask = (uint8_t)(1u << (rs.cut + 1));   /* cut_version_to_mask */
    for (; c->cmd != RTW_PWR_CMD_END; c++) {
        if (!(c->intf_mask & RTW_PWR_INTF_USB_MSK) || !(c->cut_mask & cutmask)) continue;
        switch (c->cmd) {
        case RTW_PWR_CMD_WRITE: {
            uint8_t v = (uint8_t)rd8(c->offset);
            v = (v & ~c->mask) | (c->value & c->mask);
            wr8(c->offset, v);
            break; }
        case RTW_PWR_CMD_POLLING: {
            int ok = 0;
            for (int i = 0; i < 2000; i++) { if ((rd8(c->offset) & c->mask) == (c->value & c->mask)) { ok = 1; break; } }
            if (!ok) { fprintf(stderr, "pwr poll fail off=0x%x mask=0x%x val=0x%x\n", c->offset, c->mask, c->value); return -1; }
            break; }
        case RTW_PWR_CMD_DELAY:
            if (c->value == RTW_PWR_DELAY_MS) Sleep(c->offset);
            break;
        }
    }
    return 0;
}

static void mac_pre_system_cfg(void)
{
    wr8(0x1c, 0);                                   /* REG_RSV_CTRL */
    w32_set(0x64, BIT(29) | BIT(28));               /* PAD_CTRL1: PAPE/LNAON WLBT sel */
    w32_clr(0x4c, BIT(25) | BIT(26));               /* LED_CFG */
    w32_set(0x40, BIT(2));                          /* GPIO_MUXCFG WLRFE_4_5_EN */
    w8_clr(0x02, BIT(0) | BIT(1));                  /* disable BB */
    w8_clr(0x1f, BIT(0) | BIT(1) | BIT(2));         /* disable RF */
    w32_clr(0xec, BIT(24) | BIT(25) | BIT(26));     /* WLRF1 */
}

static int mac_is_powered(void)
{
    if (rd8(0x100) == 0xea) return 0;
    if (rd8(0xf4 + 1) & BIT(0)) return 0;            /* USB: REG_SYS_STATUS1+1 bit0 */
    return 1;
}

static void rtw_detect(void)
{
    uint32_t cfg1 = rd32(0xf0);
    rs.cut = (cfg1 >> 12) & 0xf;
    rs.rf_2t2r = (cfg1 & BIT(27)) ? 1 : 0;
    if (!rs.rf_2t2r) rs.ant_rx = BB_PATH_A;
}

static int rtw_power_on(void)
{
    rtw_detect();
    printf("[pwr] SYS_CFG1=0x%08x cut=%d rf=%s  CR=0x%02x\n", rd32(0xf0), rs.cut, rs.rf_2t2r ? "2T2R" : "1T1R", rd8(0x100));

    mac_pre_system_cfg();
    if (mac_is_powered()) {
        printf("[pwr] already on -> power cycle\n");
        if (pwr_seq(trans_act_to_cardemu_8822b) || pwr_seq(trans_cardemu_to_carddis_8822b)) return -1;
        mac_pre_system_cfg();
    }
    if (pwr_seq(trans_carddis_to_cardemu_8822b) || pwr_seq(trans_cardemu_to_act_8822b)) return -1;
    w8_clr(0xf4 + 1, BIT(0));

    /* __rtw_mac_init_system_cfg (3081 path) */
    w32_set(0x1080, BIT(16) | BIT(8));              /* CPU_DMEM_CON: WL_PLATFORM_RST | DDMA_EN */
    w8_set(0x02 + 1, 0xDC);                         /* SYS_FUNC_EN+1 |= chip->sys_func_en */
    wr8(0x1100 + 3, (rd8(0x1100 + 3) & 0xF0) | 0x0C);
    uint32_t t = rd32(0x80);
    if (t & BIT(20)) { wr32(0x80, t & ~BIT(20)); w32_clr(0x40, BIT(19)); }

    wr8(0x100, 0); wr8(0x100, 0xff);                /* REG_CR = MAC_TRX_ENABLE */
    printf("[pwr] on: CR=0x%04x SYS_FUNC_EN=0x%04x 0x04=0x%08x\n", rd16(0x100), rd16(0x02), rd32(0x04));
    return 0;
}

/* ---------------- efuse */
static int rtw_read_efuse(void)
{
    static uint8_t phy[1024], log_[768];
    const int phy_size = 1024, prot = 96, log_size = 768;
    wmask(0x34, BIT(8) | BIT(9), 0);                /* bank wifi */
    wr8(0x34 + 3, rd8(0x34 + 3) & ~BIT(7));         /* LDO25 off */
    uint32_t ctl = rd32(0x30);
    for (int a = 0; a < phy_size; a++) {
        ctl &= ~(0xffu | (0x3ffu << 8));
        ctl |= (a & 0x3ff) << 8;
        wr32(0x30, ctl & ~BIT(31));
        int n = 0;
        do { ctl = rd32(0x30); if (++n > 1000) return -1; } while (!(ctl & BIT(31)));
        phy[a] = (uint8_t)(ctl & 0xff);
    }
    memset(log_, 0xff, sizeof log_);
    for (int p = 0; p < phy_size - prot;) {
        uint8_t h1 = phy[p], h2 = phy[p + 1], blk, we;
        if (h1 == 0xff || ((h1 & 0x1f) == 0xf && h2 == 0xff)) break;
        if ((h1 & 0x1f) == 0xf) { blk = ((h2 & 0xf0) >> 1) | ((h1 >> 5) & 7); we = h2 & 0xf; p += 2; }
        else { blk = (h1 & 0xf0) >> 4; we = h1 & 0xf; p += 1; }
        for (int i = 0; i < 4; i++) {
            if (we & BIT(i)) continue;
            int li = (blk << 3) + (i << 1);
            if (li + 1 >= log_size) return -2;
            log_[li] = phy[p]; log_[li + 1] = phy[p + 1]; p += 2;
        }
    }
    rs.xtal_k = log_[0xb9];
    rs.rfe_option = log_[0xca];
    memcpy(rs.mac, log_ + 0x107, 6);
    printf("[efuse] id=0x%02x%02x rfe_option=%u xtal_k=0x%02x mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
           log_[1], log_[0], rs.rfe_option, rs.xtal_k, rs.mac[0], rs.mac[1], rs.mac[2], rs.mac[3], rs.mac[4], rs.mac[5]);
    FILE *f = fopen("efuse_log.bin", "wb"); if (f) { fwrite(log_, 1, log_size, f); fclose(f); }
    return 0;
}

/* ---------------- PHY tables (rtw_parse_tbl_phy_cond) */
enum { TBL_MAC, TBL_BB, TBL_AGC, TBL_RF };
static int g_writes;
static void tbl_cfg(int kind, int path, uint32_t addr, uint32_t data)
{
    g_writes++;
    switch (kind) {
    case TBL_MAC: wr8(addr, data); break;
    case TBL_AGC: wr32(addr, data); break;
    case TBL_BB:
        if (addr == 0xfe) Sleep(50); else if (addr == 0xfd) Sleep(5); else if (addr == 0xfc) Sleep(1);
        else if (addr >= 0xf9 && addr <= 0xfb) { /* us delays */ }
        else wr32(addr, data);
        break;
    case TBL_RF:
        if (addr == 0xffe) Sleep(50); else if (addr == 0xfe) { /* 100us */ }
        else rf_write(path, addr, RFREG_MASK, data);
        break;
    }
}
static int cond_ok(uint32_t c)
{
    uint32_t rfe = c & 0xff, intf = (c >> 8) & 0xf, pkg = (c >> 12) & 0xf, cut = (c >> 24) & 0xf;
    uint32_t dcut = rs.cut ? rs.cut : 15, dpkg = 15, dintf = 2 /*INTF_USB*/;
    if (cut && cut != dcut) return 0;
    if (pkg && pkg != dpkg) return 0;
    if (intf && intf != dintf) return 0;
    if (rfe != rs.rfe_option) return 0;
    return 1;
}
static void load_table(const uint32_t *t, size_t n, int kind, int path)
{
    int matched = 1, skipped = 0; uint32_t pos = 0;
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint32_t w0 = t[i], w1 = t[i + 1];
        int is_pos = (w0 >> 31) & 1, is_neg = (w0 >> 30) & 1, branch = (w0 >> 28) & 3;
        if (is_pos) {
            if (branch == 3) { matched = 1; skipped = 0; }          /* ENDIF */
            else if (branch == 2) matched = skipped ? 0 : 1;        /* ELSE */
            else pos = w0;                                           /* IF / ELIF */
        } else if (is_neg) {
            if (!skipped) { if (cond_ok(pos)) { matched = 1; skipped = 1; } else { matched = 0; skipped = 0; } }
            else matched = 0;
        } else if (matched) {
            tbl_cfg(kind, path, w0, w1);
        }
    }
}

/* ---------------- RFE / CCA (rtw8822b_rfe_info: 2=efem, 3,5=ifem ext) */
static int rfe_is_efem(void) { return rs.rfe_option == 2; }

static void set_channel_rfe(uint8_t ch)
{
    int g2 = ch <= 14;
    if (rfe_is_efem()) {
        if (g2) { w32s_mask(0xcb0, 0xffffff, 0x705770); w32s_mask(0xcb4, 0xff00, 0x57); w32s_mask(0xcb8, BIT(4), 0); }
        else    { w32s_mask(0xcb0, 0xffffff, 0x177517); w32s_mask(0xcb4, 0xff00, 0x75); w32s_mask(0xcb8, BIT(5), 0); }
        w32s_mask(0xcbc, BIT(11) | BIT(10) | 0x3f, 0);
        if (rs.ant_rx == BB_PATH_AB || rs.ant_tx == BB_PATH_AB) w32s_mask(0xca0, 0xffff, 0xa501);
        else if (rs.ant_rx == rs.ant_tx) w32s_mask(0xca0, 0xffff, 0xa500);
        else w32s_mask(0xca0, 0xffff, 0xa005);
    } else {
        if (g2) { w32s_mask(0xcb0, 0xffffff, 0x745774); w32s_mask(0xcb4, 0xff00, 0x57); }
        else    { w32s_mask(0xcb0, 0xffffff, 0x477547); w32s_mask(0xcb4, 0xff00, 0x75); }
        w32s_mask(0xcbc, BIT(11) | BIT(10) | 0x3f, 0);
        if (g2) {
            if (rs.ant_rx == BB_PATH_AB || rs.ant_tx == BB_PATH_AB) w32s_mask(0xca0, 0xffff, 0xa501);
            else if (rs.ant_rx == rs.ant_tx) w32s_mask(0xca0, 0xffff, 0xa500);
            else w32s_mask(0xca0, 0xffff, 0xa005);
        } else w32s_mask(0xca0, 0xffff, 0xa5a5);
    }
}

static void set_channel_cca(uint8_t ch, uint8_t bw)
{
    /* rows: 1R_2G, 2R_2G, 1R_5G, 2R_5G */
    static const uint32_t ifem_ext[3][4] = {
        {0x75da8010, 0x75da8010, 0x75da8010, 0x75da8010},
        {0x79a0eaaa, 0x97A0EAAC, 0x79a0eaaa, 0x79a0eaaa},
        {0x87765541, 0x86666341, 0x87765561, 0x86666361} };
    static const uint32_t ifem[3][4] = {
        {0x75C97010, 0x75C97010, 0x75C97010, 0x75C97010},
        {0x79a0eaaa, 0x79A0EAAC, 0x79a0eaaa, 0x79a0eaaa},
        {0x87765541, 0x87746341, 0x87765541, 0x87746341} };
    static const uint32_t efem[3][4] = {
        {0x75B86010, 0x75B76010, 0x75B86010, 0x75B76010},
        {0x79A0EAA8, 0x79A0EAAC, 0x79A0EAA8, 0x79a0eaaa},
        {0x87766451, 0x87766431, 0x87766451, 0x87766431} };
    int g2 = ch <= 14, one = (rs.ant_rx == BB_PATH_A || rs.ant_rx == BB_PATH_B);
    int col = g2 ? (one ? 0 : 1) : (one ? 2 : 3);
    const uint32_t (*t)[4]; int is_ifem, is_efem, is_rfe_type = 0;
    if (rfe_is_efem()) { t = g2 ? ifem : efem; is_ifem = g2; is_efem = !g2; }
    else { t = ifem_ext; is_ifem = 1; is_efem = 0; is_rfe_type = 1; }
    uint32_t r82c = t[0][col], r830 = t[1][col], r838 = t[2][col];
    if (is_ifem) {
        if ((rs.cut == 1 && (col == 1 || col == 3) && bw == BW40) ||
            (!is_rfe_type && col == 3 && bw == BW40) || (rs.rfe_option == 5 && col == 3))
            r830 = 0x79a0ea28;
    }
    wr32(0x82c, r82c); wr32(0x830, r830); wr32(0x838, r838);
    if (is_efem && rs.cut != 1) wr32(0x83c, 0x9194b2b9);
    if (bw == BW20 && ch >= 52 && ch <= 64) wmask(0x838, 0xf0, 4);   /* IS_CH_5G_BAND_MID approx */
}

static void toggle_igi(void)
{
    uint32_t igi = rmask(0xc50, 0x7f);
    wmask(0xc50, 0x7f, igi - 2); wmask(0xc50, 0x7f, igi);
    wmask(0xe50, 0x7f, igi - 2); wmask(0xe50, 0x7f, igi);
    wmask(0x808, 0xff, 0);
    wmask(0x808, 0xff, rs.ant_rx | (rs.ant_rx << 4));
}

static int config_trx_mode(void)
{
    uint8_t tx = rs.ant_tx, rx = rs.ant_rx;
    wmask(0xc08, 0xffff, ((tx | rx) & BB_PATH_A) ? 0x3231 : 0x1111);
    wmask(0xe08, 0xffff, ((tx | rx) & BB_PATH_B) ? 0x3231 : 0x1111);
    wmask(0x93c, BIT(19) | BIT(18), 3);
    wmask(0x80c, BIT(29) | BIT(28), 1);
    wmask(0x80c, BIT(30), 1);
    if (tx & BB_PATH_A) { wmask(0x93c, 0xfff00000, 0x001); wmask(0xa04, 0xf0000000, 0x8); }
    else if (tx & BB_PATH_B) { wmask(0x93c, 0xfff00000, 0x002); wmask(0xa04, 0xf0000000, 0x4); }
    wmask(0x940, 0xfff0, (tx == BB_PATH_A || tx == BB_PATH_B) ? 0x01 : 0x43);
    wmask(0x80c, 0xff, (tx << 4) | tx);
    wmask(0xa2c, BIT(22), 0); wmask(0xa2c, BIT(18), 0);
    if (rx & BB_PATH_A) wmask(0xa04, 0x0f000000, 0);
    else if (rx & BB_PATH_B) wmask(0xa04, 0x0f000000, 5);
    wmask(0x808, 0xff, (rx << 4) | rx);
    int one = (rx == BB_PATH_A || rx == BB_PATH_B);
    wmask(0x1904, BIT(16), one ? 0 : 1); wmask(0x800, BIT(28), one ? 0 : 1); wmask(0x850, BIT(23), one ? 0 : 1);

    int cnt;
    for (cnt = 100; cnt > 0; cnt--) {
        rf_write(0, 0xef, RFREG_MASK, 0x80000);
        rf_write(0, 0x33, RFREG_MASK, 0x00001);
        if (rf_read(0, 0x33, RFREG_MASK) == 0x00001) break;
    }
    if (!cnt) { fprintf(stderr, "write RF mode table fail (RF not responding)\n"); return -1; }
    rf_write(0, 0xef, RFREG_MASK, 0x80000);
    rf_write(0, 0x33, RFREG_MASK, 0x00001);
    rf_write(0, 0x3e, RFREG_MASK, 0x00034);
    rf_write(0, 0x3f, RFREG_MASK, 0x4080c);
    rf_write(0, 0xef, RFREG_MASK, 0x00000);
    rf_write(0, 0xef, RFREG_MASK, 0x00000);
    toggle_igi();
    set_channel_cca(1, BW20);
    set_channel_rfe(rs.ch ? rs.ch : 1);
    return 0;
}

static int rtw_phy_init(void)
{
    DWORD t0 = GetTickCount();
    w8_set(0x02, BIT(0) | BIT(1));                  /* BB on */
    w8_set(0x1f, BIT(0) | BIT(1) | BIT(2));         /* RF on */
    w32_set(0xec, BIT(24) | BIT(25) | BIT(26));     /* WLRF1 BBRF_EN */
    w32_clr(0x808, BIT(28) | BIT(29));              /* RXPSEL RST pre */

    g_writes = 0;
    load_table(t8822b_mac, T8822B_MAC_N, TBL_MAC, 0);
    load_table(t8822b_bb, T8822B_BB_N, TBL_BB, 0);
    load_table(t8822b_agc, T8822B_AGC_N, TBL_AGC, 0);
    load_table(t8822b_rf_a, T8822B_RF_A_N, TBL_RF, 0);
    if (rs.rf_2t2r) load_table(t8822b_rf_b, T8822B_RF_B_N, TBL_RF, 1);
    printf("[phy] tables loaded: %d writes, %u ms\n", g_writes, GetTickCount() - t0);

    uint8_t xc = rs.xtal_k & 0x3f;
    wmask(0x24, 0x7e000000, xc);
    wmask(0x28, 0x7e, xc);
    w32_set(0x808, BIT(28) | BIT(29));              /* RXPSEL RST post */

    if (config_trx_mode()) return -1;

    /* rfe_init */
    wmask(0x64, BIT(29) | BIT(28), 3);
    wmask(0x4c, BIT(26) | BIT(25), 0);
    wmask(0x40, BIT(2), 1);
    wmask(0x1990, 0x3f, 0x30);
    wmask(0x1990, BIT(11) | BIT(10), 3);
    wmask(0x974, 0x3f, 0x3f);
    wmask(0x974, BIT(11) | BIT(10), 3);
    wr32(0x1C94, 0xAFFFAFFF);
    printf("[phy] RF A: 0x00=0x%05x 0x18=0x%05x  RF B: 0x00=0x%05x 0x18=0x%05x\n",
           rf_read(0, 0, RFREG_MASK), rf_read(0, 0x18, RFREG_MASK), rf_read(1, 0, RFREG_MASK), rf_read(1, 0x18, RFREG_MASK));
    return 0;
}

/* ---------------- channel */
static void set_channel_mac(uint8_t ch, uint8_t bw, uint8_t pri)
{
    uint8_t txsc20 = pri, txsc40 = 0;
    if (bw == BW80) txsc40 = (txsc20 == 1 || txsc20 == 3) ? 9 : 10;  /* RTW_SC_20_UPPER/UPMOST -> SC_40_UPPER */
    wr8(0x483, (txsc20 & 0xf) | ((txsc40 & 0xf) << 4));
    uint32_t v = rd32(0x668) & ~(BIT(7) | BIT(8));
    if (bw == BW80) v |= BIT(8); else if (bw == BW40) v |= BIT(7);
    wr32(0x668, v);
    v = rd32(0x24) & ~(BIT(20) | BIT(21));          /* MAC_CLK_SEL = 80M (0) */
    wr32(0x24, v);
    wr8(0x55c, 80); wr8(0x638, 80);
    uint8_t c = (uint8_t)rd8(0x454) & ~BIT(7);
    if (ch > 14) c |= BIT(7);
    wr8(0x454, c);
}

static int rtw_set_channel(uint8_t ch, uint8_t bw)
{
    static const uint8_t low_band[15] = {0x7, 0x6, 0x6, 0x5, 0x0, 0x0, 0x7, 0xff, 0x6, 0x5, 0x0, 0x0, 0x7, 0x6, 0x6};
    static const uint8_t middle_band[23] = {0x6, 0x5, 0x0, 0x0, 0x7, 0x6, 0x6, 0xff, 0x0, 0x0, 0x7, 0x6, 0x6, 0x5, 0x0, 0xff, 0x7, 0x6, 0x6, 0x5, 0x0, 0x0, 0x7};
    static const uint8_t high_band[15] = {0x5, 0x5, 0x0, 0x7, 0x7, 0x6, 0x5, 0xff, 0x0, 0x7, 0x7, 0x6, 0x5, 0x5, 0x0};
    int g2 = ch <= 14;
    uint8_t pri = 0;   /* RTW_SC_DONT_CARE for bw20 */
    rs.ch = ch; rs.bw = bw;

    /* --- set_channel_bb */
    if (g2) {
        wmask(0x808, BIT(28), 1); wmask(0x454, BIT(7), 0); wmask(0xa80, BIT(18), 0);
        wmask(0x814, 0xfc00, 15); wmask(0x958, 0x1f, 0); wmask(0x860, 0x1ffe0000, 0x96a);
        if (ch == 14) { wr32(0xa24, 0x00006577); wmask(0xa28, 0xffff, 0); }
        else { wr32(0xa24, 0x384f6577); wmask(0xa28, 0xffff, 0x1525); }
        wmask(0xcbc, 0x300, 2);
    } else {
        wmask(0xa80, BIT(18), 1); wmask(0x454, BIT(7), 1); wmask(0x808, BIT(28), 0);
        wmask(0x814, 0xfc00, 34);
        if (ch >= 36 && ch <= 64) wmask(0x958, 0x1f, 1);
        else if (ch >= 100 && ch <= 144) wmask(0x958, 0x1f, 2);
        else if (ch >= 149) wmask(0x958, 0x1f, 3);
        if (ch >= 36 && ch <= 48) wmask(0x860, 0x1ffe0000, 0x494);
        else if (ch >= 52 && ch <= 64) wmask(0x860, 0x1ffe0000, 0x453);
        else if (ch >= 100 && ch <= 116) wmask(0x860, 0x1ffe0000, 0x452);
        else if (ch >= 118 && ch <= 177) wmask(0x860, 0x1ffe0000, 0x412);
        wmask(0xcbc, 0x300, 1);
    }
    uint32_t v = rd32(0x8ac);
    switch (bw) {
    case BW40: if (pri == 1) w32_set(0xa00, BIT(4)); else w32_clr(0xa00, BIT(4));
               v = (v & 0xFF3FF300) | (((pri & 0xf) << 2) | BW40); wr32(0x8ac, v); wmask(0x8c4, BIT(30), 1); break;
    case BW80: v = (v & 0xFCEFCF00) | (((pri & 0xf) << 2) | BW80); wr32(0x8ac, v); wmask(0x8c4, BIT(30), 1);
               if (rs.rfe_option == 2 || rs.rfe_option == 3) { wmask(0x840, 0xf000, 6); wmask(0x8c8, BIT(10), 1); } break;
    case BW5:  v = (v & 0xEFEEFE00) | (BIT(6) | BW20); wr32(0x8ac, v); wmask(0x8c4, BIT(30), 0); wmask(0x8c8, BIT(31), 1); break;
    case BW10: v = (v & 0xEFFEFF00) | (BIT(7) | BW20); wr32(0x8ac, v); wmask(0x8c4, BIT(30), 0); wmask(0x8c8, BIT(31), 1); break;
    default:   v = (v & 0xFFCFFC00) | BW20; wr32(0x8ac, v); wmask(0x8c4, BIT(30), 1); break;
    }

    set_channel_mac(ch, bw, pri);

    /* --- set_channel_rf */
    uint32_t r18 = rf_read(0, 0x18, RFREG_MASK), be;
    r18 &= ~((BIT(16) | BIT(9) | BIT(8)) | 0xff | (BIT(18) | BIT(17)) | (BIT(11) | BIT(10)));
    r18 |= g2 ? 0 : (BIT(16) | BIT(8));
    r18 |= ch;
    if (ch > 144) r18 |= BIT(18); else if (ch >= 80) r18 |= BIT(17);
    r18 |= (bw == BW40) ? BIT(11) : (bw == BW80) ? BIT(10) : (BIT(11) | BIT(10));
    if (g2) be = 0;
    else if (ch >= 36 && ch <= 64) be = low_band[(ch - 36) >> 1];
    else if (ch >= 100 && ch <= 144) be = middle_band[(ch - 100) >> 1];
    else if (ch >= 149 && ch <= 177) be = high_band[(ch - 149) >> 1];
    else { fprintf(stderr, "bad channel %d\n", ch); return -1; }
    rf_write(0, 0xbe, BIT(17) | BIT(16) | BIT(15), be);
    rf_write(0, 0xdf, BIT(18), ch == 144 ? 1 : 0);
    rf_write(0, 0x18, RFREG_MASK, r18);
    if (rs.rf_2t2r) rf_write(1, 0x18, RFREG_MASK, r18);
    rf_write(0, 0xb8, BIT(19), 0);
    rf_write(0, 0xb8, BIT(19), 1);

    /* --- rxdfir */
    if (bw == BW40) { wmask(0x948, BIT(29) | BIT(28), 1); wmask(0x94c, BIT(29) | BIT(28), 0); w32s_mask(0xc20, BIT(31), 0); }
    else if (bw == BW80) { wmask(0x948, BIT(29) | BIT(28), 2); wmask(0x94c, BIT(29) | BIT(28), 1); w32s_mask(0xc20, BIT(31), 0); }
    else { wmask(0x948, BIT(29) | BIT(28), 2); wmask(0x94c, BIT(29) | BIT(28), 2); w32s_mask(0xc20, BIT(31), 1); }

    toggle_igi();
    set_channel_cca(ch, bw);
    set_channel_rfe(ch);
    Sleep(5);
    printf("[ch] ch=%d bw=%d  RF18 A=0x%05x B=0x%05x  ADCCLK=0x%08x\n", ch, 20 << bw,
           rf_read(0, 0x18, RFREG_MASK), rf_read(1, 0x18, RFREG_MASK), rd32(0x8ac));
    return 0;
}
