/* la.exe — one LA-mode snapshot to file (chip must be initialised with init.exe first).
 *   la [mode=1] [sel=3] [dma=0] [rate=1] [dbg=0] [hdr=0] [ttime=100] [pre=5] [out=la.bin] [n=1]
 * Output: raw 64-bit samples (little-endian, [lo32][hi32]) in time order; n>1 appends snapshots.
 */
#include "la.h"
#include <stdlib.h>

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
    struct la_cfg c = LA_DEFAULT;
    c.mode = (int)opt(argc, argv, "mode", c.mode); c.sel = (int)opt(argc, argv, "sel", c.sel);
    c.dma = (int)opt(argc, argv, "dma", c.dma);    c.rate = (int)opt(argc, argv, "rate", c.rate);
    c.dbg = (uint32_t)opt(argc, argv, "dbg", c.dbg); c.hdr = (uint32_t)opt(argc, argv, "hdr", c.hdr);
    c.ttime = (uint32_t)opt(argc, argv, "ttime", c.ttime); c.pre_ms = (int)opt(argc, argv, "pre", c.pre_ms);
    int nsnap = (int)opt(argc, argv, "n", 1);
    const char *out = opts(argc, argv, "out", "la.bin");
    if (nsnap < 1) { fprintf(stderr, "n must be positive\n"); return 1; }

    setvbuf(stdout, NULL, _IONBF, 0);
    if (rtw_open()) return 1;
    static uint64_t smp[LA_NSMP];
    FILE *f = fopen(out, "wb");
    if (!f) { perror(out); rtw_close(); return 1; }
    DWORD t0 = GetTickCount();
    int completed = 0, failed = 0;
    for (int s = 0; s < nsnap; s++) {
        int n = la_capture(&c, smp, nsnap == 1);
        if (n <= 0) { fprintf(stderr, "Capture failed (%d)\n", n); failed = 1; break; }
        if (fwrite(smp, 8, n, f) != (size_t)n) { perror(out); failed = 1; break; }
        completed++;
    }
    if (fclose(f)) failed = 1;
    printf("%d/%d snapshot(s) in %u ms -> %s\n", completed, nsnap, GetTickCount() - t0, out);
    rtw_close();
    return failed;
}
