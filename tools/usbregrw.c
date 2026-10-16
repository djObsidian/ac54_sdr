#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "libusb/libusb.h"
#undef interface
typedef struct libusb_device_descriptor libusb_device_descriptor;

static const unsigned short VID = 0x0b05, PID = 0x1841;

static uint32_t rread(libusb_device_handle *h, uint32_t addr, int len) {
    unsigned char buf[8];
    memset(buf, 0, sizeof buf);
    int tr = 0;
    int r = libusb_control_transfer(h, 0xC0, 0x05, addr, 0x0000, buf, len, 1000);
    if (r != len) { printf("  read 0x%x len=%d -> err %d (%s)\n", addr, len, r, libusb_error_name(r)); return 0xdeadbeef; }
    uint32_t v = 0; memcpy(&v, buf, len < 4 ? len : 4); return v;
}
static int rwrite(libusb_device_handle *h, uint32_t addr, uint32_t val, int len) {
    unsigned char buf[8]; memset(buf,0,sizeof buf); memcpy(buf, &val, len<4?len:4);
    return libusb_control_transfer(h, 0x40, 0x05, addr, 0x0000, buf, len, 500);
}

int main(void) {
    libusb_context *ctx;
    if (libusb_init(&ctx) != 0) { printf("init fail\n"); return 1; }
    libusb_device **list; int n = (int)libusb_get_device_list(ctx, &list);
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
    printf("claimed OK\n");

    printf("== read chip version regs ==\n");
    printf("  reg 0x00 (1B) = 0x%02x\n", rread(h, 0x00, 1) & 0xff);
    printf("  reg 0x01 (1B) = 0x%02x\n", rread(h, 0x01, 1) & 0xff);
    printf("  reg 0x02 (2B) = 0x%04x\n", rread(h, 0x02, 2) & 0xffff);
    printf("  reg 0x00 (4B) = 0x%08x\n", rread(h, 0x00, 4));
    printf("  reg 0x04 (4B) = 0x%08x\n", rread(h, 0x04, 4));
    printf("  reg 0x08 (4B) = 0x%08x\n", rread(h, 0x08, 4));

    printf("== read sample regs (LA not enabled, expect 0/garbage) ==\n");
    printf("  reg 0x8000 (4B) = 0x%08x\n", rread(h, 0x8000, 4));
    printf("  reg 0x8004 (4B) = 0x%08x\n", rread(h, 0x8004, 4));
    printf("  reg 0x8008 (4B) = 0x%08x\n", rread(h, 0x8008, 4));

    libusb_release_interface(h, 0);
    libusb_close(h);
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return 0;
}
