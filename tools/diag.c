#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "libusb/libusb.h"
#undef interface
typedef struct libusb_device_descriptor libusb_device_descriptor;
static const unsigned short VID=0x0b05, PID=0x1841;
static uint32_t rr(libusb_device_handle *h,uint32_t a,int l){unsigned char b[8];memset(b,0,8);int r=libusb_control_transfer(h,0xC0,0x05,a,0,b,l,1000);if(r!=l)return 0xdeadbeef;uint32_t v=0;memcpy(&v,b,l<4?l:4);return v;}
static int rw(libusb_device_handle *h,uint32_t a,uint32_t v,int l){unsigned char b[8];memset(b,0,8);memcpy(b,&v,l<4?l:4);return libusb_control_transfer(h,0x40,0x05,a,0,b,l,500);}
int main(void){
    libusb_context *ctx;if(libusb_init(&ctx)!=0)return 1;
    libusb_device **list;int n=(int)libusb_get_device_list(ctx,&list);libusb_device *dev=NULL;
    for(int i=0;i<n;i++){struct libusb_device_descriptor dd;if(libusb_get_device_descriptor(list[i],&dd)!=0)continue;if(dd.idVendor==VID&&dd.idProduct==PID){dev=list[i];break;}}
    if(!dev)return 1;libusb_device_handle *h;if(libusb_open(dev,&h)!=0)return 1;
    if(libusb_kernel_driver_active(h,0))libusb_detach_kernel_driver(h,0);
    if(libusb_claim_interface(h,0)!=0)return 1;

    printf("chip ver 0x00 = 0x%08x\n", rr(h,0x00,4));
    // liveness: read 0x8000 twice
    uint32_t a1=rr(h,0x8000,4); Sleep(60); uint32_t a2=rr(h,0x8000,4);
    printf("0x8000 t0=0x%08x t1=0x%08x  %s\n", a1, a2, (a1==a2)?"IDENTICAL (idle/const)":"DIFFERENT (live)");
    // read a few known regs
    printf("0x0044 = 0x%08x\n", rr(h,0x44,4));
    printf("0x004c = 0x%08x\n", rr(h,0x4c,4));
    printf("0x0140 = 0x%08x\n", rr(h,0x140,4));
    printf("0x07c0 = 0x%08x  (MAC IQ dump ctrl)\n", rr(h,0x7c0,4));
    printf("0x07cc = 0x%08x\n", rr(h,0x7cc,4));
    printf("0x0908 = 0x%08x  (dbg_port)\n", rr(h,0x908,4));
    printf("0x095c = 0x%08x\n", rr(h,0x95c,4));
    printf("0x09a0 = 0x%08x\n", rr(h,0x9a0,4));

    libusb_release_interface(h,0);libusb_close(h);
    libusb_free_device_list(list,1);libusb_exit(ctx);return 0;
}
