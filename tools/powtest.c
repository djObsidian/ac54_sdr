#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "libusb/libusb.h"
#undef interface
typedef struct libusb_device_descriptor libusb_device_descriptor;
static const unsigned short VID=0x0b05, PID=0x1841;
static uint32_t rr(libusb_device_handle *h,uint32_t a,int l){unsigned char b[8];memset(b,0,8);int r=libusb_control_transfer(h,0xC0,0x05,a,0,b,l,1000);if(r!=l)return 0xdeadbeef;uint32_t v=0;memcpy(&v,b,l<4?l:4);return v;}
static int rw(libusb_device_handle *h,uint32_t a,uint32_t v,int l){unsigned char b[8];memset(b,0,8);memcpy(b,&v,l<4?l:4);return libusb_control_transfer(h,0x40,0x05,a,0,b,l,500);}
static int offalive(libusb_device_handle *h){ // 1 if off-domain alive (mailbox not idle)
    return (rr(h,0x1cc,4) != 0xeaeaeaea);
}
int main(void){
    libusb_context *ctx;if(libusb_init(&ctx)!=0)return 1;
    libusb_device **list;int n=(int)libusb_get_device_list(ctx,&list);libusb_device *dev=NULL;
    for(int i=0;i<n;i++){struct libusb_device_descriptor dd;if(libusb_get_device_descriptor(list[i],&dd)!=0)continue;if(dd.idVendor==VID&&dd.idProduct==PID){dev=list[i];break;}}
    if(!dev)return 1;libusb_device_handle *h;if(libusb_open(dev,&h)!=0)return 1;
    if(libusb_kernel_driver_active(h,0))libusb_detach_kernel_driver(h,0);
    if(libusb_claim_interface(h,0)!=0)return 1;

    uint32_t pw = rr(h,0x0004,4);
    uint32_t fe = rr(h,0x0002,4);
    uint32_t clk = rr(h,0x0008,4);
    printf("SYS_PW_CTRL(0x04)=0x%08x  MAC_EN(b8)=%d MAC_OFF(b9)=%d PD(b15)=%d\n", pw,(pw>>8)&1,(pw>>9)&1,(pw>>15)&1);
    printf("SYS_FUNC_EN(0x02)=0x%08x\n", fe);
    printf("SYS_CLK_CTRL(0x8)=0x%08x\n", clk);
    printf("off-alive before = %d\n", offalive(h));

    // Try: set MAC_ENABLE (bit8), clear MAC_OFF (bit9), keep other bits
    uint32_t newpw = (pw & ~0x300) | 0x100;  // bit8=1, bit9=0
    printf("\n== write SYS_PW_CTRL = 0x%08x (MAC_EN=1, MAC_OFF=0) ==\n", newpw);
    rw(h,0x0004,newpw,4);
    Sleep(50);
    printf("SYS_PW_CTRL after = 0x%08x\n", rr(h,0x0004,4));
    printf("off-alive after  = %d\n", offalive(h));
    printf("mailbox 0x1cc    = 0x%08x\n", rr(h,0x1cc,4));
    printf("RFMOD  0x0800    = 0x%08x\n", rr(h,0x800,4));
    printf("smp    0x8000    = 0x%08x\n", rr(h,0x8000,4));

    // if not alive, also try enabling CPU clock + func en
    if (!offalive(h)) {
        printf("\n== also set SYS_FUNC_EN + CPU_CLK ==\n");
        rw(h,0x0002, fe | 0x1, 4);
        rw(h,0x0008, clk | 0x4000, 4); // BIT_CPU_CLK_EN (bit14)
        Sleep(50);
        printf("off-alive = %d  mailbox=0x%08x RFMOD=0x%08x\n", offalive(h), rr(h,0x1cc,4), rr(h,0x800,4));
    }

    libusb_release_interface(h,0);libusb_close(h);
    libusb_free_device_list(list,1);libusb_exit(ctx);return 0;
}
