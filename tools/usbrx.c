#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "libusb/libusb.h"
#undef interface
typedef struct libusb_device_descriptor libusb_device_descriptor;

static const unsigned short VID = 0x0b05, PID = 0x1841;

static void hexdump(const unsigned char *p, int n) {
    for (int i = 0; i < n && i < 128; i++) {
        if (i % 16 == 0) printf("    %04x  ", i);
        printf("%02x ", p[i]);
        if (i % 16 == 15) printf("\n");
    }
    if (n > 0 && n % 16) printf("\n");
}

int main(void) {
    libusb_context *ctx;
    if (libusb_init(&ctx) != 0) { printf("init fail\n"); return 1; }
    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, 3);

    libusb_device **list;
    int n = (int)libusb_get_device_list(ctx, &list);
    libusb_device *dev = NULL;
    for (int i = 0; i < n; i++) {
        struct libusb_device_descriptor dd;
        if (libusb_get_device_descriptor(list[i], &dd) != 0) continue;
        if (dd.idVendor == VID && dd.idProduct == PID) { dev = list[i]; break; }
    }
    if (!dev) { printf("not found\n"); return 1; }

    libusb_device_handle *h;
    if (libusb_open(dev, &h) != 0) { printf("open fail\n"); return 1; }
    if (libusb_kernel_driver_active(h, 0)) libusb_detach_kernel_driver(h, 0);
    if (libusb_claim_interface(h, 0) != 0) { printf("claim fail\n"); return 1; }
    printf("claimed. reading EP0x84 (bulk IN) for 6s...\n");

    unsigned char buf[4096];
    int total = 0, pkts = 0;
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < 6000) {
        int tr = 0;
        int r = libusb_bulk_transfer(h, 0x84, buf, sizeof buf, &tr, 400);
        if (r == 0 && tr > 0) {
            pkts++; total += tr;
            if (pkts <= 8) { printf("PKT #%d len=%d\n", pkts, tr); hexdump(buf, tr < 96 ? tr : 96); }
        } else if (r != 0 && r != LIBUSB_ERROR_TIMEOUT) {
            printf("bulk err %d\n", r);
        }
    }
    printf("== EP0x84 done: %d pkts, %d bytes ==\n", pkts, total);

    // interrupt EP 0x87
    printf("reading EP0x87 (intr IN) for 3s...\n");
    int ipkts = 0; t0 = GetTickCount();
    while (GetTickCount() - t0 < 3000) {
        int tr = 0;
        int r = libusb_bulk_transfer(h, 0x87, buf, sizeof buf, &tr, 500);
        if (r == 0 && tr > 0) { ipkts++; if (ipkts <= 4) { printf("INT #%d len=%d\n", ipkts, tr); hexdump(buf, tr < 96 ? tr : 96); } }
    }
    printf("== EP0x87 done: %d pkts ==\n", ipkts);

    libusb_release_interface(h, 0);
    libusb_close(h);
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return 0;
}
