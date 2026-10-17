/* Async EP84 diagnostic, not an IQ source.
 * bulkprobe [seconds=5] [la=0|1] -- chip must already be initialized.
 * la=1 arms ADC capture WITHOUT firing/stopping/repeating snapshots.
 * All changed registers are restored after transfers have been cancelled.
 */
#define _CRT_SECURE_NO_WARNINGS
#include "rtw.h"
#include <stdlib.h>
#define DEPTH 8
#define BYTES 65536
static struct libusb_transfer *xf[DEPTH];
static unsigned char data[DEPTH][BYTES], first[128];
static int pending, stopping, failed, first_len;
static unsigned long long received, completions, timeouts;
static volatile LONG interrupted;
static BOOL WINAPI stop_handler(DWORD event)
{
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
    InterlockedExchange(&interrupted, 1); return TRUE;
}
static void LIBUSB_CALL receive(struct libusb_transfer *t)
{
    pending--;
    if (t->status == LIBUSB_TRANSFER_COMPLETED || t->status == LIBUSB_TRANSFER_TIMED_OUT) {
        if (t->status == LIBUSB_TRANSFER_TIMED_OUT) timeouts++;
        if (t->actual_length) {
            received += t->actual_length; completions++;
            if (!first_len) {
                first_len = t->actual_length < sizeof first ? t->actual_length : sizeof first;
                memcpy(first, t->buffer, first_len);
            }
        }
    } else if (t->status != LIBUSB_TRANSFER_CANCELLED) {
        fprintf(stderr, "transfer status=%d\n", t->status); failed = stopping = 1;
    }
    if (!stopping) {
        int r = libusb_submit_transfer(t);
        if (r) { fprintf(stderr, "submit: %s\n", libusb_error_name(r)); failed = stopping = 1; }
        else pending++;
    }
}
struct saved_reg { uint16_t address; int len; uint32_t value; };
static struct saved_reg saved[] = {
    {0x100,2}, {0x116,2}, {0x290,1}, {0x10c,1}, {0x608,4},
    {0x6a0,2}, {0x6a2,2}, {0x6a4,2}, {0x60f,1}, {0x7c0,1},
    {0x8f8,4}, {0x8b4,4}, {0x95c,4}, {0x8fc,4}
};
static int put(uint32_t a, uint32_t v, int len) { return wr(a,v,len) == len ? 0 : -1; }
int main(int argc, char **argv)
{
    int seconds = 5, la = 0, count = 0;
    for (int i = 1; i < argc; ++i) {
        if (sscanf(argv[i], "seconds=%d", &seconds) == 1) continue;
        if (sscanf(argv[i], "la=%d", &la) == 1) continue;
        fprintf(stderr, "usage: bulkprobe [seconds=5] [la=0|1]\n"); return 1;
    }
    if (seconds < 1 || seconds > 3600 || la < 0 || la > 1) return 1;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (rtw_open()) return 1;
    for (; count < sizeof saved / sizeof saved[0]; count++) {
        saved[count].value = rd(saved[count].address, saved[count].len);
        if (saved[count].value == 0xdeadbeef) { failed = 1; goto cleanup; }
    }
    struct libusb_device_descriptor d;
    if (libusb_get_device_descriptor(libusb_get_device(g_h), &d)) { failed = 1; goto cleanup; }
    /* rtw88: RX FIFO 24 KB minus report reserve; enable MAC RX, USB DMA,
     * disable aggregation, accept all frame classes for transport sanity. */
    if (put(0x7c0,0,1) || put(0x116,24576-129,2) ||
        put(0x290,d.bcdUSB >= 0x300 ? 0x0e : 0x1e,1) ||
        put(0x10c,saved[3].value & ~4u,1) ||
        put(0x608,0x7000600f,4) || put(0x6a0,0xffff,2) ||
        put(0x6a2,0xffff,2) || put(0x6a4,0xffff,2) ||
        put(0x60f,4,1) || put(0x100,saved[0].value | 0x20a,2)) {
        failed = 1; goto cleanup;
    }
    SetConsoleCtrlHandler(stop_handler, TRUE);
    for (int i = 0; i < DEPTH; i++) {
        xf[i] = libusb_alloc_transfer(0);
        if (!xf[i]) { failed = 1; goto cancel; }
        libusb_fill_bulk_transfer(xf[i],g_h,0x84,data[i],BYTES,receive,NULL,500);
        int r = libusb_submit_transfer(xf[i]);
        if (r) { fprintf(stderr,"submit: %s\n",libusb_error_name(r)); failed = 1; goto cancel; }
        pending++;
    }
    if (la) {
        if (put(0x8f8,saved[10].value & ~0x3c00000u,4) ||
            put(0x8b4,saved[11].value | 0x80,4) ||
            put(0x95c,(saved[12].value & ~0x80000fffu) | 0x23,4) ||
            put(0x8fc,0,4) || put(0x7c0,0xcb,1)) {
            failed = 1; goto cancel;
        }
    }
    printf("Async bulk EP84: %d x %d B, LA=%d, %d seconds (payload is NOT assumed IQ)\n",DEPTH,BYTES,la,seconds);
    ULONGLONG start = GetTickCount64(), last = start;
    while (!stopping && !InterlockedCompareExchange(&interrupted,0,0) &&
           GetTickCount64() - start < (ULONGLONG)seconds * 1000) {
        struct timeval tv = {0,50000};
        int r = libusb_handle_events_timeout(g_ctx,&tv);
        if (r && r != LIBUSB_ERROR_INTERRUPTED) { failed = 1; break; }
        if (GetTickCount64()-last >= 1000) {
            printf("bytes=%llu completions=%llu timeouts=%llu\n",received,completions,timeouts);
            last = GetTickCount64();
        }
    }
cancel:
    stopping = 1;
    for (int i = 0; i < DEPTH; i++) if (xf[i]) libusb_cancel_transfer(xf[i]);
    while (pending) { struct timeval tv = {0,50000}; libusb_handle_events_timeout(g_ctx,&tv); }
    for (int i = 0; i < DEPTH; i++) if (xf[i]) libusb_free_transfer(xf[i]);
    SetConsoleCtrlHandler(stop_handler, FALSE);
    printf("total=%llu bytes, completions=%llu, timeouts=%llu; LA=%08x RXDMA=%08x\n",
           received,completions,timeouts,rd32(0x7c0),rd32(0x288));
    for (int i = 0; i < first_len; i++) printf("%02x%s",first[i],(i%16 == 15 || i == first_len-1) ? "\n" : " ");
cleanup:
    while (count--) if (put(saved[count].address,saved[count].value,saved[count].len)) failed = 1;
    rtw_close();
    return failed ? 1 : received ? 0 : 2;
}
