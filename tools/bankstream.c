/* Live bank-scheduled LA -> C2H. Never read a bank while ADC writes it.
 * Prefix via EP0 avoids a C2H descriptor crossing into the active bank.
 * No ADC pauses, dropped samples, or synthetic replacement samples. */
#define _CRT_SECURE_NO_WARNINGS
#include "rtwinit.h"
#include "iqstream.h"
#include "directio.h"
int main(int argc,char **argv) {
    int rate=6,seconds=3,result=1,async=0;const char *path="NUL";
    for(int i=1;i<argc;i++){
        if(sscanf(argv[i],"rate=%d",&rate)==1)continue;
        if(sscanf(argv[i],"seconds=%d",&seconds)==1)continue;
        if(sscanf(argv[i],"async=%d",&async)==1)continue;
        if(!strncmp(argv[i],"out=",4)){path=argv[i]+4;continue;}return 1;
    }
    if(rate<1||rate>6||seconds<1||seconds>3600||async<0||async>1)return 1;
    static unsigned char bankbuf[65536],piece[16376],rx[65536];
    static int16_t iq[8192*2];struct iq_freshness fresh={0};
    struct libusb_transfer *transfer=NULL;struct iq_bulk_wait wait={1,0,0};FILE *file=NULL;
    struct control_queue controls={0};for(int i=0;i<3;i++)controls.wait[i].done=1;
    double fs=80000000.0/(1u<<rate),period=8192/fs,begin=0,maxcost=0;
    unsigned long long banks=0,wanted=(unsigned long long)(seconds*fs)/8192;
    unsigned overlaps=0;int priority=GetThreadPriority(GetCurrentThread());
    setvbuf(stdout,NULL,_IONBF,0);SetConsoleCtrlHandler(on_stop,TRUE);
    if(rtw_open())return 1;
    if(rd8(0xff)!=0x20||rtw_power_on()||rtw_read_efuse()||rtw_phy_init()||rtw_set_channel(11,BW20))goto done;
    if(put(0x100,rd16(0x100)|0x8a,2)||put(0x608,(rd32(0x608)|0x80000)&~0xffffu,4)||
       put(0x6a0,0,2)||put(0x6a2,0,2)||put(0x6a4,0,2)||put(0x290,14,1)||
       put(0x10c,rd8(0x10c)&~4u,1)||put(0x106,0x69,1))goto done;
    for(int j=0;j<32;j++){int n;if(libusb_bulk_transfer(g_h,0x84,rx,sizeof rx,&n,10))break;}
    file=fopen(path,"wb");if(!file)goto done;
    transfer=libusb_alloc_transfer(0);if(!transfer)goto done;
    if(async)for(int i=0;i<3;i++){controls.transfer[i]=libusb_alloc_transfer(0);if(!controls.transfer[i])goto done;}
    libusb_fill_bulk_transfer(transfer,g_h,0x84,rx,sizeof rx,iq_bulk_done,&wait,100);
    if(put(0x7c0,0,1)||put(0x8f8,rd32(0x8f8)&~0x3c00000u,4)||put(0x8b4,rd32(0x8b4)|0x80,4)||
       put(0x95c,(rd32(0x95c)&~0x80000fffu)|((unsigned)rate<<5)|3,4)||put(0x8fc,0,4))goto done;
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_HIGHEST);
    if(put(0x7c0,0xcb,1))goto done;begin=now();
    printf("BANK LIVE fs=%.0f bank_ms=%.3f\n",fs,period*1000);
    while(banks<wanted&&!InterlockedCompareExchange(&stopped,0,0)) {
        double target=begin+(banks+1.1)*period,deadline=begin+(banks+1.9)*period;
        while(now()<target)YieldProcessor();
        if(now()>=deadline){fprintf(stderr,"Bank start late bank=%llu\n",banks);goto done;}
        double start=now();unsigned base=(unsigned)(banks&1)*65536;
        /* Prefix permits exactly four maximum-length C2H reads, each with
         * 32 exact overlap samples. RX descriptor length is 14 bits. */
        if(put(0x140,0x7a0+(base>>12),2))goto done;
        for(unsigned off=0;off<1056;){unsigned n=1056-off;if(n>504)n=504;
            if(la_blk_rd(0x8000+off,bankbuf+off,n)!=(int)n)goto done;off+=n;}
        unsigned pos=800,available=1056;
        while(pos<65536) {
            unsigned size=65536-pos;if(size>sizeof piece)size=sizeof piece;
            if(direct_fetch(base+pos,size,piece,transfer,&wait,rx,async?&controls:NULL))goto done;
            if(memcmp(bankbuf+pos,piece,available-pos)){
                fprintf(stderr,"Bank overlap failure bank=%llu offset=%u\n",banks,pos);goto done;
            }overlaps++;
            memcpy(bankbuf+pos,piece,size);available=pos+size;
            if(available==65536)break;
            pos=available-OVERLAP*8;
        }
        double cost=now()-start;if(cost>maxcost)maxcost=cost;
        if(now()>=deadline){fprintf(stderr,"Bank completion late bank=%llu cost_ms=%.3f\n",banks,cost*1000);goto done;}
        if(iq_check_freshness(&fresh,bankbuf,8192,banks*8192))goto done;
        for(unsigned j=0;j<8192;j++){uint64_t v;memcpy(&v,bankbuf+j*8,8);iq[j*2]=(int16_t)(la_i(v)*16);iq[j*2+1]=(int16_t)(la_q(v)*16);}
        if(fwrite(iq,sizeof iq,1,file)!=1)goto done;banks++;
    }result=0;
done:
    printf("%s banks=%llu samples=%llu RF_s=%.6f wall_s=%.6f max_fetch_ms=%.3f overlaps=%u freshness=%u\n",result?"FAILED":"OK",banks,banks*8192,banks*8192/fs,begin?now()-begin:0,maxcost*1000,overlaps,fresh.checks);
    for(int i=0;i<3;i++)if(controls.transfer[i]){
        if(!controls.wait[i].done){libusb_cancel_transfer(controls.transfer[i]);while(!controls.wait[i].done){struct timeval tv={0,10000};libusb_handle_events_timeout(g_ctx,&tv);}}
        libusb_free_transfer(controls.transfer[i]);
    }
    if(transfer){if(!wait.done){libusb_cancel_transfer(transfer);while(!wait.done){struct timeval tv={0,10000};libusb_handle_events_timeout(g_ctx,&tv);}}libusb_free_transfer(transfer);}
    if(file&&fclose(file))result=1;SetThreadPriority(GetCurrentThread(),priority);SetConsoleCtrlHandler(on_stop,FALSE);
    if(rtw_power_on()||rtw_phy_init()||rtw_set_channel(11,BW20))result=1;rtw_close();return result;
}
