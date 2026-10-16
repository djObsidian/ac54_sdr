#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "libusb/libusb.h"
#undef interface
typedef struct libusb_device_descriptor libusb_device_descriptor;
static const unsigned short VID=0x0b05, PID=0x1841;
static uint32_t rr(libusb_device_handle *h,uint32_t a,int l){unsigned char b[8];memset(b,0,8);int r=libusb_control_transfer(h,0xC0,0x05,a,0,b,l,1000);if(r!=l)return 0xdeadbeef;uint32_t v=0;memcpy(&v,b,l<4?l:4);return v;}
static int rw(libusb_device_handle *h,uint32_t a,uint32_t v,int l){unsigned char b[8];memset(b,0,8);memcpy(b,&v,l<4?l:4);return libusb_control_transfer(h,0x40,0x05,a,0,b,l,500);}
static void h2c(libusb_device_handle *h, uint32_t box, uint32_t w0, uint32_t w1){
    uint32_t hmetfr; int tries=0;
    do { hmetfr = rr(h,0x1cc,4); tries++; Sleep(1); } while ((hmetfr & (1u<<box)) && tries<30);
    rw(h, 0x1f0 + 4*box, w1, 4);
    rw(h, 0x1d0 + 4*box, w0, 4);
}
int main(void){
    libusb_context *ctx;if(libusb_init(&ctx)!=0)return 1;
    libusb_device **list;int n=(int)libusb_get_device_list(ctx,&list);libusb_device *dev=NULL;
    for(int i=0;i<n;i++){struct libusb_device_descriptor dd;if(libusb_get_device_descriptor(list[i],&dd)!=0)continue;if(dd.idVendor==VID&&dd.idProduct==PID){dev=list[i];break;}}
    if(!dev)return 1;libusb_device_handle *h;if(libusb_open(dev,&h)!=0)return 1;
    if(libusb_kernel_driver_active(h,0))libusb_detach_kernel_driver(h,0);
    if(libusb_claim_interface(h,0)!=0)return 1;

    // 1) power on off-domain
    uint32_t pw = rr(h,0x0004,4);
    rw(h,0x0004,(pw & ~0x300)|0x100,4);
    Sleep(30);
    printf("off alive: mailbox=0x%08x RFMOD=0x%08x\n", rr(h,0x1cc,4), rr(h,0x800,4));

    // 2) H2C power ACTIVE + channel ch1 bw20M
    h2c(h,0,0x01000020,0);
    h2c(h,1,0x00010166,0);
    Sleep(50);

    // 3) enable RF mode (CCKEN bit24, OFDMEN bit25) in REG_FPGA0_RFMOD 0x800
    uint32_t rfm = rr(h,0x800,4);
    printf("RFMOD before = 0x%08x (CCK=%d OFDM=%d)\n", rfm,(rfm>>24)&1,(rfm>>25)&1);
    rw(h,0x800, rfm | 0x3000000, 4); // set CCKEN+OFDMEN
    Sleep(50);
    printf("RFMOD after  = 0x%08x (CCK=%d OFDM=%d)\n", rr(h,0x800,4),(rr(h,0x800,4)>>24)&1,(rr(h,0x800,4)>>25)&1);

    // 4) check sample buffer
    printf("\n== sample buffer 0x8000 (liveness over 300ms) ==\n");
    int changed=0; uint32_t prev=rr(h,0x8000,4);
    for(int i=0;i<10;i++){ uint32_t v=rr(h,0x8000,4); if(v!=prev)changed=1; prev=v; printf("  t=%d I=0x%08x\n", i*30, v); Sleep(30); }
    printf("sample buffer %s\n", changed?"CHANGING (live!)":"STATIC");

    // also check a few other off-domain regs
    printf("\n== off-domain status ==\n");
    printf("0x07c0 (IQdump) = 0x%08x\n", rr(h,0x7c0,4));
    printf("0x0910 (PSD)    = 0x%08x\n", rr(h,0x910,4));
    printf("0x08ac (ADCCLK) = 0x%08x\n", rr(h,0x8ac,4));
    printf("0x08fc (DBGSEL) = 0x%08x\n", rr(h,0x8fc,4));
    printf("0x0900 (ANTSEL) = 0x%08x\n", rr(h,0x900,4));

    libusb_release_interface(h,0);libusb_close(h);
    libusb_free_device_list(list,1);libusb_exit(ctx);return 0;
}
