#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "libusb/libusb.h"
#undef interface
typedef struct libusb_device_descriptor libusb_device_descriptor;

static const unsigned short VID = 0x0b05, PID = 0x1841;

static uint32_t rr(libusb_device_handle *h, uint32_t addr, int len) {
    unsigned char buf[8]; memset(buf,0,sizeof buf);
    int r = libusb_control_transfer(h, 0xC0, 0x05, addr, 0, buf, len, 1000);
    if (r != len) return 0xdeadbeef;
    uint32_t v=0; memcpy(&v, buf, len<4?len:4); return v;
}
static int rw(libusb_device_handle *h, uint32_t addr, uint32_t val, int len) {
    unsigned char buf[8]; memset(buf,0,sizeof buf); memcpy(buf,&val,len<4?len:4);
    return libusb_control_transfer(h, 0x40, 0x05, addr, 0, buf, len, 500);
}
static void dump3(libusb_device_handle *h, const char*tag) {
    printf("  %-22s 0x8000=0x%08x 0x8004=0x%08x 0x8008=0x%08x\n", tag,
           rr(h,0x8000,4), rr(h,0x8004,4), rr(h,0x8008,4));
}

int main(void) {
    libusb_context *ctx; if (libusb_init(&ctx)!=0) return 1;
    libusb_device **list; int n=(int)libusb_get_device_list(ctx,&list);
    libusb_device *dev=NULL;
    for (int i=0;i<n;i++){ struct libusb_device_descriptor dd; if(libusb_get_device_descriptor(list[i],&dd)!=0)continue; if(dd.idVendor==VID&&dd.idProduct==PID){dev=list[i];break;} }
    if(!dev)return 1;
    libusb_device_handle *h; if(libusb_open(dev,&h)!=0)return 1;
    if(libusb_kernel_driver_active(h,0))libusb_detach_kernel_driver(h,0);
    if(libusb_claim_interface(h,0)!=0)return 1;

    printf("== baseline (LA off) ==\n"); dump3(h,"idle");

    // Try a few LA-mode enable combos (mode 0=BB, dbg_port sweep)
    for (int dbg = 0; dbg <= 4; dbg++) {
        // set dbg_port (reg 0x908)
        rw(h, 0x908, dbg, 4);
        // trigger time (reg 0x7cc + 0x7c0) - small value
        rw(h, 0x7cc, 0, 4);
        rw(h, 0x7c0, 0x40, 4);   // bit6 = some trig
        // MAC IQ dump enable (reg 0x7c0 bit0 + bits6-7=dbg)
        rw(h, 0x7c0, 1 | (dbg & 0x3) << 6, 4);
        // BB config reg 0x95c
        rw(h, 0x95c, 0, 4);
        Sleep(30);
        char tag[32]; sprintf(tag, "dbg=%d", dbg);
        dump3(h, tag);
    }

    // Now sweep reads at several offsets to see a pattern (noise?)
    printf("== offset sweep (dbg=0 state) ==\n");
    for (int off = 0; off < 16; off += 2) {
        printf("  +0x%x: I=0x%08x Q=0x%08x\n", off, rr(h,0x8000+off,4), rr(h,0x8004+off,4));
    }

    libusb_release_interface(h,0); libusb_close(h);
    libusb_free_device_list(list,1); libusb_exit(ctx);
    return 0;
}
