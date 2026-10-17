#pragma once
/* Experimental continuous RTL8822BU LA ring -> DDMA -> RX SRAM -> USB bulk.
 * Caller owns the open, initialized USB handle. No trigger/rearm or snapshots.
 * seconds=0 runs until sink returns 1 (normal stop) or -1 (failure).
 * 1984 new samples + 32 overlapping samples per fetch; exact overlap checked.
 * On any discontinuity, stop and return failure. Output is signed LE int16 IQ.
 */
#define _CRT_SECURE_NO_WARNINGS
#include "la.h"
#include "iqfresh.h"
#include <stdlib.h>
#define STEP 1984
#define OVERLAP 32
#define SAMPLES (STEP+OVERLAP)
#define PAYLOAD (SAMPLES*8)
#define PACKET (24+PAYLOAD)
static volatile LONG stopped;
struct iq_bulk_wait {int done,status,length;};
static void LIBUSB_CALL iq_bulk_done(struct libusb_transfer *t)
{
    struct iq_bulk_wait *w=(struct iq_bulk_wait*)t->user_data;
    w->status=t->status;w->length=t->actual_length;w->done=1;
}
static BOOL WINAPI on_stop(DWORD e) {if(e!=CTRL_C_EVENT && e!=CTRL_BREAK_EVENT)return FALSE;InterlockedExchange(&stopped,1);return TRUE;}
static double now(void) {LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return (double)t.QuadPart/f.QuadPart;}
static int put(unsigned a,unsigned v,int n) {return wr(a,v,n)==n?0:-1;}
static int copy(unsigned src,unsigned dst,unsigned len)
{
    /* The three channel registers are consecutive; one vendor transfer writes
     * source, destination, then OWN/length last instead of three USB round trips. */
    uint32_t command[3]={src,dst,0x80000000|len};
    if(la_blk_wr(0x1200,command,sizeof command)!=sizeof command)return -1;
    uint32_t c=rd32(0x1208);
    double deadline=now()+0.003;
    while(c!=0xdeadbeef && (c&0x80000000) && now()<deadline)c=rd32(0x1208);
    if(c==0xdeadbeef)return -1;
    if(c&0x80000000) {fprintf(stderr,"DDMA busy %08x\n",c);return -1;}
    return 0;
}
static int iq_stream_run(int seconds,int rate,const char *path, int (*sink)(const int16_t *,int,void *),void *user)
{
    int result=1,memsaved=0;
    if(rate<3||rate>6||seconds<0||(!path&&!sink))return 1;
    int oldpriority=GetThreadPriority(GetCurrentThread());
    InterlockedExchange(&stopped,0);
    double fs=80000000.0/(1u<<rate),period=STEP/fs,ring=LA_NSMP/fs;
    unsigned regs[]={0x100,0x608,0x290,0x10c,0x106,0x140,0x294,0x1080,0x1200,0x1204,0x1208,0x8f8,0x8b4,0x95c,0x8fc,0x7c0,0x6a0,0x6a2,0x6a4};
    int widths[]={2,4,1,1,1,2,4,4,4,4,4,4,4,4,4,1,2,2,2};
    unsigned backup[19];int saved=0;
    struct iq_freshness fresh={0};
    unsigned char oldmem[PACKET], header[24]={0},rx[65536],tail[OVERLAP*8];
    int16_t iq[STEP*2];FILE *file=NULL;
    struct libusb_transfer *transfer=NULL;
    struct iq_bulk_wait wait={1,0,0};
    unsigned long long blocks=0;double begin=0,elapsed=0,maxcost=0;
    if(rd8(0xff)!=0x20){fprintf(stderr,"USB3 required\n");goto done;}
    for(;saved<19;saved++){backup[saved]=rd(regs[saved],widths[saved]);if(backup[saved]==0xdeadbeef)goto done;}
    if((backup[6]&0x10000)||(backup[10]&0x80000000)||(backup[15]&2)){fprintf(stderr,"Capture/DMA busy; reinitialize first\n");goto done;}
    if(path){file=fopen(path,"wb");if(!file){perror(path);goto done;}}
    /* MAC RX must remain enabled for the ADC writer; filter frames instead. */
    if(put(0x100,backup[0]|0x8a,2)||put(0x608,(backup[1]|0x80000)&~0xffffu,4)||
       put(0x6a0,0,2)||put(0x6a2,0,2)||put(0x6a4,0,2)||put(0x290,0xe,1)||
       put(0x10c,backup[3]&~4u,1)||put(0x1080,backup[7]|0x10100,4)||put(0x106,0x69,1))goto done;
    /* RX SRAM window is page 0x700; save the staging region at 0x1000. */
    for(unsigned off=0;off<PACKET;) {
        unsigned n=PACKET-off;if(n>504)n=504;if(n>4096-(off&4095))n=4096-(off&4095);
        if(put(0x140,0x701+(off>>12),2)||la_blk_rd(0x8000+(off&4095),oldmem+off,(uint16_t)n)!=(int)n)goto done;
        off+=n;
    }memsaved=1;
    header[0]=PAYLOAD&255;header[1]=PAYLOAD>>8;header[3]=0x80;header[11]=0x10;
    if(put(0x140,0x701,2)||la_blk_wr(0x8000,header,24)!=24)goto done;
    /* Flush old Wi-Fi traffic after applying receive filters. */
    for(int j=0;j<32;j++){int n=0;if(libusb_bulk_transfer(g_h,0x84,rx,sizeof rx,&n,10))break;}
    transfer=libusb_alloc_transfer(0);if(!transfer)goto done;
    libusb_fill_bulk_transfer(transfer,g_h,0x84,rx,sizeof rx,iq_bulk_done,&wait,100);
    if(put(0x7c0,0,1)||put(0x8f8,backup[11]&~0x3c00000u,4)||put(0x8b4,backup[12]|0x80,4)||
       put(0x95c,(backup[13]&~0x80000fffu)|((unsigned)rate<<5)|3,4)||put(0x8fc,0,4))goto done;
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_HIGHEST);
    if(put(0x7c0,0xcb,1))goto done;
    begin=now();double target=begin+ring*0.5;
    printf("CONTINUOUS ring capture %.0f samples/s; ring %.3f ms; overlap %d samples\n",fs,ring*1000,OVERLAP);
    unsigned long long wanted=seconds?(unsigned long long)(seconds*fs)/STEP:~0ull;
    while(blocks<wanted && !InterlockedCompareExchange(&stopped,0,0)) {
        while(now()<target)YieldProcessor();
        double t=now();
        if(t-target>ring*0.25){fprintf(stderr,"Host late %.3f ms; stopping before overwrite\n",(t-target)*1000);goto done;}
        unsigned off=(unsigned)((blocks*STEP)%LA_NSMP)*8;
        unsigned part=LA_SIZE-off;if(part>PAYLOAD)part=PAYLOAD;
        /* Queue the host read before C2H is published, avoiding a further
         * request submission round trip after the device has data ready. */
        wait.done=0;wait.length=0;
        if(libusb_submit_transfer(transfer)){wait.done=1;goto done;}
        if(copy(0x18780000+LA_START+off,0x18701018,part))goto done;
        if(part<PAYLOAD && copy(0x18780000+LA_START,0x18701018+part,PAYLOAD-part))goto done;
        if(now()-target>ring*0.45){fprintf(stderr,"DMA missed ring deadline\n");goto done;}
        if(put(0x294,0x11000,4))goto done;
        while(!wait.done){struct timeval tv={0,10000};int e=libusb_handle_events_timeout(g_ctx,&tv);if(e && e!=LIBUSB_ERROR_INTERRUPTED)goto done;}
        int n=wait.length,r=wait.status==LIBUSB_TRANSFER_COMPLETED?0:wait.status;
        if(r || n!=PACKET || memcmp(rx,header,12)) {
            fprintf(stderr,"bulk r=%d bytes=%d expected=%d hdr=%02x%02x%02x%02x\n",r,n,PACKET,rx[0],rx[1],rx[2],rx[3]);goto done;
        }
        if(blocks && memcmp(tail,rx+24,sizeof tail)) {
            fprintf(stderr,"DISCONTINUITY at sample %llu (overlap differs)\n",blocks*STEP);goto done;
        }
        if(iq_check_freshness(&fresh,rx+24,STEP,blocks*STEP))goto done;
        memcpy(tail,rx+24+STEP*8,sizeof tail);
        for(int j=0;j<STEP;j++){uint64_t v;memcpy(&v,rx+24+j*8,8);iq[j*2]=(int16_t)(la_i(v)*16);iq[j*2+1]=(int16_t)(la_q(v)*16);}
        if(file && fwrite(iq,sizeof iq,1,file)!=1){perror(path);goto done;}
        if(sink){int s=sink(iq,STEP,user);if(s){result=s<0?1:0;goto done;}}
        blocks++;target+=period;
        double cost=now()-t;if(cost>maxcost)maxcost=cost;
    }
    result=0;
done:
    if(begin)elapsed=now()-begin;
    if(transfer){if(!wait.done){libusb_cancel_transfer(transfer);while(!wait.done){struct timeval tv={0,10000};libusb_handle_events_timeout(g_ctx,&tv);}}libusb_free_transfer(transfer);}
    if(saved==19){wr8(0x7c0,0);wr32(0x294,backup[6]&~0x10000u);wr32(0x1208,0);}
    if(memsaved)for(unsigned off=0;off<PACKET;) {
        unsigned n=PACKET-off;if(n>504)n=504;if(n>4096-(off&4095))n=4096-(off&4095);
        if(put(0x140,0x701+(off>>12),2)||la_blk_wr(0x8000+(off&4095),oldmem+off,(uint16_t)n)!=(int)n)result=1;
        off+=n;
    }
    while(saved--)if(put(regs[saved],backup[saved],widths[saved]))result=1;
    if(file && fclose(file))result=1;
    SetThreadPriority(GetCurrentThread(),oldpriority);
    printf("%s: %llu samples, %.6f s RF, %.6f s wall, max fetch %.3f ms -> %s\n",result?"FAILED":"OK",blocks*STEP,blocks*STEP/fs,elapsed,maxcost*1000,path?path:"sink");
    printf("freshness checks=%u\n",fresh.checks);
    return result;
}
