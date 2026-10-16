#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "libusb/libusb.h"
#undef interface
typedef struct libusb_device_descriptor libusb_device_descriptor;
static const unsigned short VID=0x0b05, PID=0x1841;
static uint32_t rr(libusb_device_handle *h,uint32_t a,int l){unsigned char b[8];memset(b,0,8);int r=libusb_control_transfer(h,0xC0,0x05,a,0,b,l,1000);if(r!=l)return 0xdeadbeef;uint32_t v=0;memcpy(&v,b,l<4?l:4);return v;}
static int rw(libusb_device_handle *h,uint32_t a,uint32_t v,int l){unsigned char b[8];memset(b,0,8);memcpy(b,&v,l<4?l:4);return libusb_control_transfer(h,0x40,0x05,a,0,b,l,500);}

// send H2C mailbox command: word1(EX) first, then word0(main)
static void h2c(libusb_device_handle *h, uint32_t box, uint32_t w0, uint32_t w1){
    // poll REG_HMETFR (0x1cc) for box availability (up to 30ms)
    uint32_t hmetfr; int tries=0;
    do { hmetfr = rr(h,0x1cc,4); tries++; Sleep(1); } while ((hmetfr & (1u<<box)) && tries<30);
    rw(h, 0x1f0 + 4*box, w1, 4);   // EX
    rw(h, 0x1d0 + 4*box, w0, 4);   // main (triggers)
}

int main(void){
    libusb_context *ctx;if(libusb_init(&ctx)!=0)return 1;
    libusb_device **list;int n=(int)libusb_get_device_list(ctx,&list);libusb_device *dev=NULL;
    for(int i=0;i<n;i++){struct libusb_device_descriptor dd;if(libusb_get_device_descriptor(list[i],&dd)!=0)continue;if(dd.idVendor==VID&&dd.idProduct==PID){dev=list[i];break;}}
    if(!dev)return 1;libusb_device_handle *h;if(libusb_open(dev,&h)!=0)return 1;
    if(libusb_kernel_driver_active(h,0))libusb_detach_kernel_driver(h,0);
    if(libusb_claim_interface(h,0)!=0)return 1;

    printf("== mailbox status (0x1cc HMETFR) = 0x%08x\n", rr(h,0x1cc,4));
    printf("== HMEBOX0 (0x1d0) before = 0x%08x\n", rr(h,0x1d0,4));

    // baseline off-domain
    printf("\n== off-domain baseline ==\n");
    printf("0x0800 (RFMOD) = 0x%08x\n", rr(h,0x800,4));
    printf("0x080c (TXPSEL)= 0x%08x\n", rr(h,0x80c,4));
    printf("0x07c0 (IQdump)= 0x%08x\n", rr(h,0x7c0,4));
    printf("0x8000 (smp I) = 0x%08x\n", rr(h,0x8000,4));

    // H2C power mode ACTIVE (0x20): mode=0,rlbm=0,smart=0,awake=1
    printf("\n== send H2C power(ACTIVE) box0 ==\n");
    h2c(h, 0, 0x01000020, 0x00000000);
    printf("HMEBOX0 after = 0x%08x\n", rr(h,0x1d0,4));

    // H2C channel info (0x66): link=1, ch=1, bw=0(20M)
    printf("== send H2C channel(ch1,bw20M) box1 ==\n");
    h2c(h, 1, 0x00010166, 0x00000000);
    printf("HMEBOX1 after = 0x%08x\n", rr(h,0x1d4,4));

    Sleep(100);
    printf("\n== off-domain after H2C ==\n");
    printf("0x0800 (RFMOD) = 0x%08x\n", rr(h,0x800,4));
    printf("0x07c0 (IQdump)= 0x%08x\n", rr(h,0x7c0,4));
    printf("0x8000 (smp I) = 0x%08x\n", rr(h,0x8000,4));
    printf("0x12c (CPWM)   = 0x%08x\n", rr(h,0x12c,4));

    libusb_release_interface(h,0);libusb_close(h);
    libusb_free_device_list(list,1);libusb_exit(ctx);return 0;
}
