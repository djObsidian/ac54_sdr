#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "libusb/libusb.h"
#undef interface
typedef struct libusb_device_descriptor libusb_device_descriptor;
static const unsigned short VID=0x0b05, PID=0x1841;
static uint32_t rr(libusb_device_handle *h,uint32_t a,int l){unsigned char b[8];memset(b,0,8);int r=libusb_control_transfer(h,0xC0,0x05,a,0,b,l,1000);if(r!=l)return 0xdeadbeef;uint32_t v=0;memcpy(&v,b,l<4?l:4);return v;}
static int rw(libusb_device_handle *h,uint32_t a,uint32_t v,int l){unsigned char b[8];memset(b,0,8);memcpy(b,&v,l<4?l:4);return libusb_control_transfer(h,0x40,0x05,a,0,b,l,500);}
static void poweron(libusb_device_handle *h){ uint32_t pw=rr(h,0x0004,4); rw(h,0x0004,(pw&~0x300)|0x100,4); Sleep(30); }
static int sample_live(libusb_device_handle *h){
    uint32_t prev=rr(h,0x8000,4); for(int i=0;i<6;i++){uint32_t v=rr(h,0x8000,4); if(v!=prev) return 1; prev=v; Sleep(20);} return 0;
}
int main(void){
    libusb_context *ctx;if(libusb_init(&ctx)!=0)return 1;
    libusb_device **list;int n=(int)libusb_get_device_list(ctx,&list);libusb_device *dev=NULL;
    for(int i=0;i<n;i++){struct libusb_device_descriptor dd;if(libusb_get_device_descriptor(list[i],&dd)!=0)continue;if(dd.idVendor==VID&&dd.idProduct==PID){dev=list[i];break;}}
    if(!dev)return 1;libusb_device_handle *h;if(libusb_open(dev,&h)!=0)return 1;
    if(libusb_kernel_driver_active(h,0))libusb_detach_kernel_driver(h,0);
    if(libusb_claim_interface(h,0)!=0)return 1;
    poweron(h);
    printf("off alive: mailbox=0x%08x RFMOD=0x%08x 0x8000=0x%08x\n", rr(h,0x1cc,4), rr(h,0x800,4), rr(h,0x8000,4));

    printf("\n== LA-mode dbg_port sweep (0x908) + MAC IQ dump (0x7c0) ==\n");
    for (int dbg=0; dbg<16; dbg++) {
        rw(h,0x908,dbg,4);            // set dbg_port
        rw(h,0x7c0,0,4);             // clear
        rw(h,0x7c0,1|((dbg&0x3)<<6),4); // enable bit0 + dbg bits6-7
        rw(h,0x7cc,0,4);             // trigger time
        rw(h,0x95c,0,4);             // BB config
        Sleep(25);
        uint32_t s=rr(h,0x8000,4);
        int live = (s!=0xeaeaeaea) ? 1 : sample_live(h);
        printf("  dbg=%2d  0x908=0x%02x  0x7c0=0x%02x  0x8000=0x%08x %s\n", dbg, rr(h,0x908,4)&0xff, rr(h,0x7c0,4)&0xff, s, live?"*** LIVE ***":"");
    }
    libusb_release_interface(h,0);libusb_close(h);
    libusb_free_device_list(list,1);libusb_exit(ctx);return 0;
}
