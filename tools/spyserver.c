/* spyserver.exe — expose the RTL8822BU LA-mode I/Q capture as a SpyServer (SDR++ / SDR#).
 *
 *   spyserver [port=5555] [init=1] [dma=0] [dc=1] [verbose=0]
 *
 * SDR++: Source = "SpyServer", host 127.0.0.1 port 5555, Connect, pick sample rate, Play.
 *
 * How it works
 *  - capture thread: tune (5 MHz channel raster) -> LA snapshot (16384 x 12-bit IQ @ 40 MS/s, ~410 us)
 *    -> DC removal -> digital shift by the residual (requested - channel centre) -> FIR decimation
 *    -> "loop buffer" with crossfaded ends; ~12 snapshots/s.
 *  - network thread: streams the newest loop buffer at the real-time rate the client expects
 *    (the snapshot is repeated until the next one is ready). This is a SNAPSHOT receiver:
 *    good for spectrum/waterfall, NOT for demodulating continuous signals.
 *  - Frequencies: SpyServer uses uint32 Hz (max 4.29 GHz). 5 GHz is reached with a 4 GHz offset:
 *    requested f < 2.0 GHz is treated as f + 4 GHz (set "Offset" = 4000 MHz in SDR++ for 5 GHz).
 */
#define WIN32_LEAN_AND_MEAN
#define _USE_MATH_DEFINES
#include <winsock2.h>
#include <ws2tcpip.h>
#include <math.h>
#include <stdlib.h>
#include "rtwinit.h"
#include "la.h"
#pragma comment(lib, "ws2_32.lib")

/* ---------------- SpyServer protocol (Youssef Touil, see SDR++ spyserver_protocol.h) */
#define SS_PROTO_VERSION ((2u << 24) | (0u << 16) | 1700u)
enum { CMD_HELLO = 0, CMD_SET_SETTING = 2, CMD_PING = 3 };
enum { SET_STREAMING_MODE = 0, SET_STREAMING_ENABLED = 1, SET_GAIN = 2,
       SET_IQ_FORMAT = 100, SET_IQ_FREQUENCY = 101, SET_IQ_DECIMATION = 102, SET_IQ_DIGITAL_GAIN = 103,
       SET_FFT_FORMAT = 200, SET_FFT_FREQUENCY = 201, SET_FFT_DECIMATION = 202 };
enum { FMT_UINT8 = 1, FMT_INT16 = 2, FMT_INT24 = 3, FMT_FLOAT = 4 };
enum { MSG_DEVICE_INFO = 0, MSG_CLIENT_SYNC = 1, MSG_PONG = 2,
       MSG_UINT8_IQ = 100, MSG_INT16_IQ = 101, MSG_FLOAT_IQ = 103 };
#define DEV_RTLSDR 3

#pragma pack(push, 1)
struct ss_hdr { uint32_t proto, type, stream, seq, size; };
struct ss_devinfo { uint32_t DeviceType, DeviceSerial, MaximumSampleRate, MaximumBandwidth, DecimationStageCount,
                    GainStageCount, MaximumGainIndex, MinimumFrequency, MaximumFrequency, Resolution,
                    MinimumIQDecimation, ForcedIQFormat; };
struct ss_sync { uint32_t CanControl, Gain, DeviceCenterFrequency, IQCenterFrequency, FFTCenterFrequency,
                 MinimumIQCenterFrequency, MaximumIQCenterFrequency, MinimumFFTCenterFrequency, MaximumFFTCenterFrequency; };
#pragma pack(pop)

#define FS_MAX      40000000.0
#define DECIM_MAX   5            /* 40, 20, 10, 5, 2.5, 1.25 MS/s */
#define F_OFFSET_5G 4000000000.0

/* ---------------- shared state */
typedef struct { float re, im; } cf;
static CRITICAL_SECTION g_lock;
static volatile uint32_t g_req_freq = 2437000000u;   /* as sent by client (uint32) */
static volatile int g_decim = 0;
static volatile LONG g_quit;
static cf *g_loop; static int g_loop_n; static int g_loop_decim = -1; static volatile LONG g_loop_id;
static int g_dc = 1, g_verbose = 0;
static struct la_cfg g_la;

static double real_freq(uint32_t f) { return f < 2000000000u ? f + F_OFFSET_5G : (double)f; }

static int choose_channel(double f, double *fc)
{
    int ch;
    if (f < 3e9) {
        if (f >= 2480e6) { *fc = 2484e6; return 14; }
        ch = (int)floor((f - 2407e6) / 5e6 + 0.5);
        if (ch < 1) ch = 1; if (ch > 13) ch = 13;
        *fc = 2407e6 + 5e6 * ch;
    } else {
        ch = (int)floor((f - 5000e6) / 5e6 + 0.5);
        if (ch < 32) ch = 32; if (ch > 177) ch = 177;
        *fc = 5000e6 + 5e6 * ch;
    }
    return ch;
}

/* windowed-sinc low-pass, cutoff 0.45 * fs_out */
static float *make_fir(int D, int *ntaps)
{
    int n = 16 * D + 1; float *h = malloc(sizeof(float) * n); double fc = 0.45 / D, s = 0;
    for (int i = 0; i < n; i++) {
        double m = i - (n - 1) / 2.0;
        double w = 0.42 - 0.5 * cos(2 * M_PI * i / (n - 1)) + 0.08 * cos(4 * M_PI * i / (n - 1));
        double x = (m == 0) ? 2 * fc : sin(2 * M_PI * fc * m) / (M_PI * m);
        h[i] = (float)(x * w); s += h[i];
    }
    for (int i = 0; i < n; i++) h[i] = (float)(h[i] / s);
    *ntaps = n; return h;
}

static DWORD WINAPI capture_thread(void *arg)
{
    (void)arg;
    static uint64_t raw[LA_NSMP];
    static cf x[LA_NSMP];
    double fc = 0; uint32_t tuned_req = 0; int nsnap = 0; DWORD t0 = GetTickCount();
    float *fir = NULL; int ntaps = 0, fir_d = -1;
    while (!g_quit) {
        uint32_t req = g_req_freq; int decim = g_decim;
        if (req != tuned_req) {
            double f = real_freq(req);
            int ch = choose_channel(f, &fc);
            rtw_set_channel((uint8_t)ch, BW20);
            tuned_req = req;
            printf("tune %.3f MHz -> ch %d (%.0f MHz), residual %+.3f MHz\n", f / 1e6, ch, fc / 1e6, (f - fc) / 1e6);
        }
        int n = la_capture(&g_la, raw, g_verbose);
        if (n < 0) { fprintf(stderr, "USB error, capture thread exits\n"); break; }
        if (n < 1024) continue;

        /* to complex float, DC removal */
        double mi = 0, mq = 0;
        for (int i = 0; i < n; i++) { x[i].re = (float)la_i(raw[i]); x[i].im = (float)la_q(raw[i]); mi += x[i].re; mq += x[i].im; }
        if (g_dc) { mi /= n; mq /= n; for (int i = 0; i < n; i++) { x[i].re -= (float)mi; x[i].im -= (float)mq; } }

        /* shift by residual so that the requested frequency lands at DC */
        double res = real_freq(req) - fc;
        if (fabs(res) > 1.0) {
            double w = -2 * M_PI * res / FS_MAX;
            for (int i = 0; i < n; i++) {
                float c = (float)cos(w * i), s = (float)sin(w * i);
                float re = x[i].re * c - x[i].im * s, im = x[i].re * s + x[i].im * c;
                x[i].re = re; x[i].im = im;
            }
        }

        /* decimate */
        int D = 1 << decim, m;
        cf *y;
        if (D == 1) { m = n; y = malloc(sizeof(cf) * m); memcpy(y, x, sizeof(cf) * m); }
        else {
            if (fir_d != D) { free(fir); fir = make_fir(D, &ntaps); fir_d = D; }
            m = (n - ntaps) / D; if (m < 64) continue;
            y = malloc(sizeof(cf) * m);
            for (int k = 0; k < m; k++) {
                float re = 0, im = 0; const cf *p = x + k * D;
                for (int t = 0; t < ntaps; t++) { re += p[t].re * fir[t]; im += p[t].im * fir[t]; }
                y[k].re = re; y[k].im = im;
            }
        }

        /* loop buffer: crossfade tail into head so that y[L..m) + loop is continuous */
        int L = m / 8, ln = m - L;
        cf *lp = malloc(sizeof(cf) * ln);
        for (int k = 0; k < ln; k++) lp[k] = y[k];
        for (int k = 0; k < L; k++) {
            float w = 0.5f - 0.5f * (float)cos(M_PI * (k + 0.5) / L);   /* 0 -> 1 */
            lp[k].re = y[k].re * w + y[ln + k].re * (1 - w);
            lp[k].im = y[k].im * w + y[ln + k].im * (1 - w);
        }
        free(y);
        EnterCriticalSection(&g_lock);
        free(g_loop); g_loop = lp; g_loop_n = ln; g_loop_decim = decim;
        LeaveCriticalSection(&g_lock);
        InterlockedIncrement(&g_loop_id);
        if (++nsnap % 50 == 0) printf("snapshots: %d (%.1f/s)\n", nsnap, nsnap * 1000.0 / (GetTickCount() - t0));
    }
    return 0;
}

/* ---------------- network */
static int send_all(SOCKET s, const void *b, int n)
{
    const char *p = b;
    while (n > 0) { int r = send(s, p, n, 0); if (r <= 0) return -1; p += r; n -= r; }
    return 0;
}
static uint32_t g_seq;
static int send_msg(SOCKET s, uint32_t type, uint32_t stream, const void *body, uint32_t size)
{
    struct ss_hdr h = { SS_PROTO_VERSION, type, stream, g_seq++, size };
    if (send_all(s, &h, sizeof h)) return -1;
    return size ? send_all(s, body, (int)size) : 0;
}
static int send_sync(SOCKET s)
{
    struct ss_sync y = { 1, 0, g_req_freq, g_req_freq, g_req_freq, 1100000000u, 2600000000u, 1100000000u, 2600000000u };
    return send_msg(s, MSG_CLIENT_SYNC, 0, &y, sizeof y);
}

static void serve_client(SOCKET s)
{
    int streaming = 0, fmt = FMT_INT16, mode = 1;
    uint8_t cmdbuf[512]; int cmdlen = 0;
    static uint8_t out[1 << 20];
    LARGE_INTEGER qf, q0, qn; QueryPerformanceFrequency(&qf);
    double sent = 0; long cur_id = -1; int pos = 0; cf *loop = NULL; int loop_n = 0;
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof one);
    int sb = 8 << 20; setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char *)&sb, sizeof sb);

    for (;;) {
        /* --- commands */
        int r = recv(s, (char *)cmdbuf + cmdlen, (int)sizeof cmdbuf - cmdlen, 0);
        if (r == 0) break;
        if (r < 0 && WSAGetLastError() != WSAEWOULDBLOCK) break;
        if (r > 0) cmdlen += r;
        while (cmdlen >= 8) {
            uint32_t ct, bs; memcpy(&ct, cmdbuf, 4); memcpy(&bs, cmdbuf + 4, 4);
            if (bs > 256) { cmdlen = 0; break; }
            if (cmdlen < (int)(8 + bs)) break;
            uint8_t *b = cmdbuf + 8;
            if (ct == CMD_HELLO) {
                uint32_t ver; memcpy(&ver, b, 4);
                printf("client HELLO ver=0x%08x app='%.*s'\n", ver, (int)bs - 4, b + 4);
                struct ss_devinfo di = { DEV_RTLSDR, 0x88220001u, (uint32_t)FS_MAX, 20000000u, DECIM_MAX,
                                         0, 0, 1100000000u, 2600000000u, 12, 0, 0 };
                nb = 0; ioctlsocket(s, FIONBIO, &nb);
                send_msg(s, MSG_DEVICE_INFO, 0, &di, sizeof di);
                send_sync(s);
                nb = 1; ioctlsocket(s, FIONBIO, &nb);
            } else if (ct == CMD_SET_SETTING && bs >= 8) {
                uint32_t set, val; memcpy(&set, b, 4); memcpy(&val, b + 4, 4);
                switch (set) {
                case SET_STREAMING_MODE: mode = (int)val; break;
                case SET_STREAMING_ENABLED:
                    streaming = val != 0; sent = 0; QueryPerformanceCounter(&q0);
                    printf("streaming %s (fmt %d, decim %d -> %.3f MS/s)\n", streaming ? "ON" : "OFF", fmt, g_decim, FS_MAX / (1 << g_decim) / 1e6);
                    break;
                case SET_IQ_FORMAT: fmt = (int)val; break;
                case SET_IQ_FREQUENCY:
                    if (val) { g_req_freq = val; nb = 0; ioctlsocket(s, FIONBIO, &nb); send_sync(s); nb = 1; ioctlsocket(s, FIONBIO, &nb); }
                    break;
                case SET_IQ_DECIMATION:
                    g_decim = (int)(val > DECIM_MAX ? DECIM_MAX : val); sent = 0; QueryPerformanceCounter(&q0);
                    break;
                default: if (g_verbose) printf("setting %u = %u (ignored)\n", set, val);
                }
            } else if (ct == CMD_PING) {
                nb = 0; ioctlsocket(s, FIONBIO, &nb); send_msg(s, MSG_PONG, 0, NULL, 0); nb = 1; ioctlsocket(s, FIONBIO, &nb);
            }
            memmove(cmdbuf, cmdbuf + 8 + bs, cmdlen - 8 - bs); cmdlen -= 8 + bs;
        }

        if (!streaming || !(mode & 1)) { Sleep(5); continue; }

        /* --- pick up newest loop buffer (switch only at wrap to keep continuity inside a loop) */
        if (g_loop_id != cur_id && (pos == 0 || !loop)) {
            EnterCriticalSection(&g_lock);
            if (g_loop && g_loop_decim == g_decim) {
                free(loop); loop = malloc(sizeof(cf) * g_loop_n); memcpy(loop, g_loop, sizeof(cf) * g_loop_n);
                loop_n = g_loop_n; cur_id = g_loop_id; pos = 0;
            }
            LeaveCriticalSection(&g_lock);
        }
        if (!loop) { Sleep(5); continue; }

        /* --- real-time pacing */
        double fs = FS_MAX / (1 << g_decim);
        QueryPerformanceCounter(&qn);
        double due = fs * (double)(qn.QuadPart - q0.QuadPart) / qf.QuadPart - sent;
        if (due > fs * 0.2) { sent += due - fs * 0.05; due = fs * 0.05; }    /* fell behind: drop */
        int bps = fmt == FMT_UINT8 ? 2 : fmt == FMT_FLOAT ? 8 : 4;
        int maxn = (int)(sizeof out / bps);
        if (due < fs * 0.002) { Sleep(1); continue; }
        int cnt = (int)due; if (cnt > maxn) cnt = maxn;

        for (int k = 0; k < cnt; k++) {
            cf v = loop[pos];
            if (++pos >= loop_n) {
                pos = 0;
                if (g_loop_id != cur_id) {           /* newer snapshot available: switch at wrap */
                    EnterCriticalSection(&g_lock);
                    if (g_loop && g_loop_decim == g_decim) {
                        free(loop); loop = malloc(sizeof(cf) * g_loop_n); memcpy(loop, g_loop, sizeof(cf) * g_loop_n);
                        loop_n = g_loop_n; cur_id = g_loop_id;
                    }
                    LeaveCriticalSection(&g_lock);
                }
            }
            if (fmt == FMT_UINT8) {
                int i = (int)(v.re / 16) + 128, q = (int)(v.im / 16) + 128;
                out[2 * k] = (uint8_t)(i < 0 ? 0 : i > 255 ? 255 : i); out[2 * k + 1] = (uint8_t)(q < 0 ? 0 : q > 255 ? 255 : q);
            } else if (fmt == FMT_FLOAT) {
                float f2[2] = { v.re / 2048.0f, v.im / 2048.0f }; memcpy(out + 8 * k, f2, 8);
            } else {
                float a = v.re * 16, b2 = v.im * 16;
                int16_t s2[2] = { (int16_t)(a > 32767 ? 32767 : a < -32768 ? -32768 : a),
                                  (int16_t)(b2 > 32767 ? 32767 : b2 < -32768 ? -32768 : b2) };
                memcpy(out + 4 * k, s2, 4);
            }
        }
        uint32_t mt = fmt == FMT_UINT8 ? MSG_UINT8_IQ : fmt == FMT_FLOAT ? MSG_FLOAT_IQ : MSG_INT16_IQ;
        nb = 0; ioctlsocket(s, FIONBIO, &nb);
        int e = send_msg(s, mt, 1, out, (uint32_t)(cnt * bps));
        nb = 1; ioctlsocket(s, FIONBIO, &nb);
        if (e) break;
        sent += cnt;
    }
    free(loop);
    printf("client disconnected\n");
}

static long opt(int argc, char **argv, const char *k, long def)
{
    size_t kl = strlen(k);
    for (int i = 1; i < argc; i++)
        if (!strncmp(argv[i], k, kl) && argv[i][kl] == '=') return strtol(argv[i] + kl + 1, NULL, 0);
    return def;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("Capture mode: repeated LA snapshots; NOT continuous RF IQ. USB3 does not change this.");
    int port = (int)opt(argc, argv, "port", 5555);
    g_dc = (int)opt(argc, argv, "dc", 1); g_verbose = (int)opt(argc, argv, "verbose", 0);
    g_la = LA_DEFAULT; g_la.dma = (int)opt(argc, argv, "dma", 0);

    if (rtw_open()) return 1;
    rtw_detect();
    if (opt(argc, argv, "init", 1)) {
        rs.verbose = 1;
        if (rtw_power_on() || rtw_read_efuse() || rtw_phy_init()) { fprintf(stderr, "chip init failed\n"); return 1; }
        rs.verbose = 0;
    } else if (rtw_read_efuse()) return 1;

    InitializeCriticalSection(&g_lock);
    CreateThread(NULL, 0, capture_thread, NULL, 0, NULL);

    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int one = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof one);
    struct sockaddr_in a = { 0 }; a.sin_family = AF_INET; a.sin_port = htons((u_short)port); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(ls, (struct sockaddr *)&a, sizeof a) || listen(ls, 1)) { fprintf(stderr, "bind/listen :%d failed\n", port); return 1; }
    printf("SpyServer listening on 0.0.0.0:%d  (40 MS/s / 2^n, n=0..%d; 5 GHz: f-4000 MHz)\n", port, DECIM_MAX);
    for (;;) {
        SOCKET c = accept(ls, NULL, NULL);
        if (c == INVALID_SOCKET) continue;
        printf("client connected\n");
        serve_client(c);
        closesocket(c);
    }
}
