/* Isolate live LA SRAM changes: baseline reads, descriptor write, C2H.
 * At rate 6, compare bank 0 (active) with bank 1 (stable) at 16..19 ms.
 * Never interprets data outside that deadline as corruption evidence. */
#define _CRT_SECURE_NO_WARNINGS
#include "rtwinit.h"
#include "iqstream.h"
static unsigned bank;
static int readpage(unsigned char *p) { return txff_xfer(0x21000+bank*0x10000,p,512,0); }
static unsigned differences(const unsigned char *a,const unsigned char *b,const char *stage,int trial) {
    unsigned n=0;
    for(unsigned j=24;j<512;j+=8)if(memcmp(a+j,b+j,8)){
        uint64_t x,y;memcpy(&x,a+j,8);memcpy(&y,b+j,8);
        if(n<2 && trial<4)printf("trial=%d stage=%s byte=%u %016llx -> %016llx\n",trial,stage,j,x,y);n++;
    }return n;
}
int main(int argc,char **argv) {
    int mode=argc>1?atoi(argv[1]):0, trials=argc>2?atoi(argv[2]):100,result=1,arb=argc>3?atoi(argv[3]):0;
    if(mode<0||mode>3||trials<1||trials>1000||arb<0||arb>1)return 1;
    bank=argc>4?atoi(argv[4]):0;if(bank>1)return 1;
    unsigned char a[512],b[512],c[512],rx[8192],header[24]={0};
    header[0]=0xe8;header[1]=0x01;header[3]=0x80;header[11]=0x10;
    unsigned changed_write=0,changed_read=0,bulk_diff=0,late=0,valid=0;double max=0;
    setvbuf(stdout,NULL,_IONBF,0);if(rtw_open())return 1;
    if(rtw_power_on()||rtw_read_efuse()||rtw_phy_init()||rtw_set_channel(11,BW20))goto done;
    if(put(0x100,rd16(0x100)|0x8a,2)||put(0x608,(rd32(0x608)|0x80000)&~0xffffu,4)||
       put(0x6a0,0,2)||put(0x6a2,0,2)||put(0x6a4,0,2)||put(0x290,14,1)||put(0x10c,(rd8(0x10c)&~5u)|arb,1))goto done;
    for(int i=0;i<32;i++){int n;if(libusb_bulk_transfer(g_h,0x84,rx,sizeof rx,&n,10))break;}
    if(put(0x8f8,rd32(0x8f8)&~0x3c00000u,4)||put(0x8b4,rd32(0x8b4)|0x80,4)||
       put(0x95c,(rd32(0x95c)&~0x80000fffu)|(6u<<5)|3,4)||put(0x8fc,0,4))goto done;
    for(int i=0;i<trials;i++) {
        if(put(0x7c0,0,1)||put(0x7c0,0xcb,1))goto done;
        double start=now();while(now()-start<0.016)YieldProcessor();
        if(readpage(a))goto done;
        if(mode&& (put(0x140,0x7a1+bank*16,2)||la_blk_wr(0x8000,header,24)!=24))goto done;
        if(readpage(b))goto done;
        if(mode==3&&put(0x7c0,0,1))goto done;
        if(mode>=2){
            int n=0;if(put(0x294,bank?0x17000:0x01017000,4)||libusb_bulk_transfer(g_h,0x84,rx,sizeof rx,&n,100)||n!=512)goto done;
        }
        if(readpage(c))goto done;
        double elapsed=now()-start;if(elapsed>max)max=elapsed;
        if(put(0x7c0,0,1))goto done;
        if(elapsed>=0.019){late++;continue;}
        valid++;changed_write+=differences(a,b,"write",i);changed_read+=differences(b,c,"C2H",i);
        if(mode>=2)bulk_diff+=differences(b,rx,"bulk",i);
    }result=0;
done:
    printf("mode=%d arb=%d bank=%u result=%d valid=%u late=%u changed_write=%u changed_read=%u bulk_diff=%u max_ms=%.3f\n",mode,arb,bank,result,valid,late,changed_write,changed_read,bulk_diff,max*1000);
    if(rtw_power_on()||rtw_phy_init()||rtw_set_channel(11,BW20))result=1;
    rtw_close();return result;
}
