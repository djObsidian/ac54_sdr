/* reg.exe — interactive register tool.
 *   reg r  <addr> [len=4] [count=1]     read (count consecutive)
 *   reg w  <addr> <val> [len=4]          write
 *   reg m  <addr> <mask> <val>           masked RMW (val is shifted to mask LSB)
 *   reg d  <addr> <bytes>                hexdump (32-bit reads)
 * Several commands may be chained with ','  e.g.  reg w 0x106 0x69 1 , r 0x7c0
 */
#include "rtw.h"
#include <stdlib.h>

static uint32_t num(const char *s) { return (uint32_t)strtoul(s, NULL, 0); }

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: reg r|w|m|d ... [, cmd ...]\n"); return 1; }
    if (rtw_open()) return 1;
    int i = 1;
    while (i < argc) {
        int j = i; while (j < argc && strcmp(argv[j], ",") != 0) j++;
        int n = j - i; char **a = argv + i;
        if (n >= 2 && a[0][0] == 'r') {
            uint32_t ad = num(a[1]); int l = n > 2 ? (int)num(a[2]) : 4; int c = n > 3 ? (int)num(a[3]) : 1;
            for (int k = 0; k < c; k++) printf("0x%04x = 0x%0*x\n", ad + k * l, l * 2, rd(ad + k * l, l));
        } else if (n >= 3 && a[0][0] == 'w') {
            uint32_t ad = num(a[1]), v = num(a[2]); int l = n > 3 ? (int)num(a[3]) : 4;
            int r = wr(ad, v, l); printf("W 0x%04x <- 0x%x (len %d) rc=%d\n", ad, v, l, r);
        } else if (n >= 4 && a[0][0] == 'm') {
            uint32_t ad = num(a[1]), m = num(a[2]), v = num(a[3]);
            wmask(ad, m, v); printf("M 0x%04x [0x%08x] <- 0x%x  => 0x%08x\n", ad, m, v, rd32(ad));
        } else if (n >= 3 && a[0][0] == 'd') {
            uint32_t ad = num(a[1]), len = num(a[2]);
            for (uint32_t o = 0; o < len; o += 16) {
                printf("%04x:", ad + o);
                for (int k = 0; k < 16 && o + k < len; k += 4) printf(" %08x", rd32(ad + o + k));
                printf("\n");
            }
        } else if (n >= 3 && a[0][0] == 'b') {   /* block read in ONE control transfer */
            uint32_t ad = num(a[1]), len = num(a[2]);
            static unsigned char buf[4096];
            int r = libusb_control_transfer(g_h, 0xC0, 0x05, (uint16_t)ad, 0, buf, (uint16_t)len, 1000);
            printf("block read 0x%04x len %u -> rc=%d\n", ad, len, r);
            for (int o = 0; o < r; o += 16) {
                printf("%04x:", ad + o);
                for (int k = 0; k < 16 && o + k < r; k += 4) { uint32_t v; memcpy(&v, buf + o + k, 4); printf(" %08x", v); }
                printf("\n");
            }
        } else { fprintf(stderr, "bad cmd near arg %d\n", i); }
        i = j + 1;
    }
    rtw_close();
    return 0;
}
