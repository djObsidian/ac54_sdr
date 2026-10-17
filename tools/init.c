/* init.exe — bring up RTL8822BU BB/RF without firmware and tune.
 *   init [ch=6] [bw=0(20)/1(40)/2(80)] [pwr=1] [phy=1] [rfe=N override]
 */
#include "rtwinit.h"
#include <stdlib.h>

static long opt(int argc, char **argv, const char *k, long def)
{
    size_t kl = strlen(k);
    for (int i = 1; i < argc; i++)
        if (!strncmp(argv[i], k, kl) && argv[i][kl] == '=') return strtol(argv[i] + kl + 1, NULL, 0);
    return def;
}

int main(int argc, char **argv)
{
    int ch = (int)opt(argc, argv, "ch", 6), bw = (int)opt(argc, argv, "bw", 0);
    if (rtw_open()) return 1;
    rtw_detect();
    rs.verbose = 1;
    if (opt(argc, argv, "pwr", 1) && rtw_power_on()) { fprintf(stderr, "power on failed\n"); return 1; }
    if (rtw_read_efuse()) { fprintf(stderr, "efuse read failed\n"); return 1; }
    long rfe = opt(argc, argv, "rfe", -1);
    if (rfe >= 0) rs.rfe_option = (uint8_t)rfe;
    if (opt(argc, argv, "phy", 1) && rtw_phy_init()) { fprintf(stderr, "phy init failed\n"); return 1; }
    if (rtw_set_channel((uint8_t)ch, (uint8_t)bw)) return 1;
    printf("[done] IGI A=0x%02x B=0x%02x  RXPSEL=0x%08x  CCK/OFDM(0x808)=0x%08x\n",
           rmask(0xc50, 0x7f), rmask(0xe50, 0x7f), rd32(0x808), rd32(0x800));
    rtw_close();
    return 0;
}
