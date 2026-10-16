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

    printf("== write+readback test ==\n");
    printf("0x7c0 before = 0x%08x\n", rr(h,0x7c0,4));
    rw(h,0x7c0,0xA5A5A5A5,4);
    printf("0x7c0 after  = 0x%08x  (wrote 0xa5a5a5a5)\n", rr(h,0x7c0,4));
    rw(h,0x7c0,0x00000000,4);
    printf("0x7c0 after0 = 0x%08x  (wrote 0)\n", rr(h,0x7c0,4));

    printf("\n== 0x8000 refresh test (enable capture, watch over 300ms) ==\n");
    // full LA enable attempt
    rw(h,0x908,0,4);       // dbg_port
    rw(h,0x7cc,0,4);       // trig time
    rw(h,0x7c0,1,4);       // enable bit0
    for(int i=0;i<8;i++){
        uint32_t v=rr(h,0x8000,4);
        printf("  t=%d 0x8000=0x%08x\n", i*40, v);
        Sleep(40);
    }
    libusb_release_interface(h,0);libusb_close(h);
    libusb_free_device_list(list,1);libusb_exit(ctx);return 0;
}
