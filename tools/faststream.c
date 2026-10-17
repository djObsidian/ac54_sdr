/* Experimental live LA -> direct C2H, without DDMA. Host-written descriptor
 * occupies three samples immediately BEFORE each fetched interval; those
 * samples have already been consumed (or will be rewritten before consumption).
 * Exact overlaps, freshness, and conservative writer deadlines are mandatory.
 * WARNING: live C2H currently corrupts some samples; this is a diagnostic tool,
 * not a validated continuous stream. Use iqstream for validated live capture.
 */
#define _CRT_SECURE_NO_WARNINGS
#include "rtwinit.h"
#include "iqstream.h"
#include "directio.h"
int main(int argc,char **argv) {
    int rate=5,seconds=10,result=1,async=0,dmamode=14;const char *path="NUL";
    for(int i=1;i<argc;i++) {
        if(sscanf(argv[i],"rate=%d",&rate)==1)continue;
        if(sscanf(argv[i],"seconds=%d",&seconds)==1)continue;
        if(sscanf(argv[i],"async=%d",&async)==1)continue;
        if(sscanf(argv[i],"guard=%d",&guard)==1)continue;
        if(sscanf(argv[i],"dmamode=%d",&dmamode)==1)continue;
        if(!strncmp(argv[i],"out=",4)){path=argv[i]+4;continue;}
        fprintf(stderr,"usage: faststream [rate=1..6] [seconds=1..3600] [async=0|1] [guard=0|1, requires async=0] [dmamode=0|2|12|14] [out=filename.cs16]\n");return 1;
    }
    if(rate<1||rate>6||seconds<1||seconds>3600||async<0||async>1||guard<0||guard>1||(guard&&async))return 1;
    if(dmamode!=0&&dmamode!=2&&dmamode!=12&&dmamode!=14)return 1;
    unsigned char rx[65536],payload[PAYLOAD],tail[OVERLAP*8];int16_t iq[STEP*2];
    struct iq_freshness fresh={0};
    struct libusb_transfer *transfer=NULL;struct iq_bulk_wait wait={1,0,0};FILE *file=NULL;
    struct control_queue controls={0};for(int i=0;i<3;i++)controls.wait[i].done=1;
    double fs=80000000.0/(1u<<rate),ring=LA_NSMP/fs,period=STEP/fs,begin=0,maxcost=0;
    unsigned long long blocks=0,wanted=(unsigned long long)(seconds*fs)/STEP;
    int priority=GetThreadPriority(GetCurrentThread());
    SetConsoleCtrlHandler(on_stop,TRUE);InterlockedExchange(&stopped,0);
    setvbuf(stdout,NULL,_IONBF,0);if(rtw_open())return 1;
    fprintf(stderr,"EXPERIMENTAL direct C2H: live SRAM corruption remains unresolved\n");
    if(rd8(0xff)!=0x20||rtw_power_on()||rtw_read_efuse()||rtw_phy_init()||rtw_set_channel(11,BW20))goto done;
    /* MAC RX must stay enabled: disabling it freezes the ADC writer on 8822B.
     * Reject ordinary Wi-Fi frames with receive filters instead. */
    if(put(0x100,rd16(0x100)|0x8a,2)||put(0x608,(rd32(0x608)|0x80000)&~0xffffu,4)||
       put(0x6a0,0,2)||put(0x6a2,0,2)||put(0x6a4,0,2)||
       put(0x290,dmamode,1)||put(0x10c,rd8(0x10c)&~4u,1)||put(0x106,0x69,1))goto done;
    for(int j=0;j<32;j++){int n=0;if(libusb_bulk_transfer(g_h,0x84,rx,sizeof rx,&n,10))break;}
    file=fopen(path,"wb");if(!file)goto done;
    transfer=libusb_alloc_transfer(0);if(!transfer)goto done;
    if(async)for(int i=0;i<3;i++){controls.transfer[i]=libusb_alloc_transfer(0);if(!controls.transfer[i])goto done;}
    libusb_fill_bulk_transfer(transfer,g_h,0x84,rx,sizeof rx,iq_bulk_done,&wait,100);
    if(put(0x7c0,0,1)||put(0x8f8,rd32(0x8f8)&~0x3c00000u,4)||put(0x8b4,rd32(0x8b4)|0x80,4)||
       put(0x95c,(rd32(0x95c)&~0x80000fffu)|((unsigned)rate<<5)|3,4)||put(0x8fc,0,4))goto done;
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_HIGHEST);
    if(put(0x7c0,0xcb,1))goto done;
    begin=now();double target=begin+ring*0.5;
    printf("DIRECT LIVE fs=%.0f ring_ms=%.3f overlap=%d dmamode=%d\n",fs,ring*1000,OVERLAP,dmamode);
    while(blocks<wanted&&!InterlockedCompareExchange(&stopped,0,0)) {
        while(now()<target)YieldProcessor();
        double t=now();
        /* Permit catch-up only inside the unchanged final 0.45-ring deadline. */
        if(t-target>ring*0.40){fprintf(stderr,"Host late %.3f ms\n",(t-target)*1000);goto done;}
        unsigned off=(unsigned)((blocks*STEP)%LA_NSMP)*8;
        unsigned remain=PAYLOAD,pos=0;
        while(remain) {
            /* A C2H packet cannot cross the 0x26000 alias discontinuity.
             * Split also at the logical ring end for simple validation. */
            unsigned part=0x10000-(off&0xffff);if(part>remain)part=remain;
            if(direct_fetch(off,part,payload+pos,transfer,&wait,rx,async?&controls:NULL))goto done;
            remain-=part;pos+=part;off=(off+part)%LA_SIZE;
        }
        if(now()-target>ring*0.45){fprintf(stderr,"Direct read missed writer deadline\n");goto done;}
        if(blocks&&memcmp(tail,payload,sizeof tail)){
            unsigned mismatch=0;while(mismatch<sizeof tail&&tail[mismatch]==payload[mismatch])mismatch++;
            uint64_t a,b;memcpy(&a,tail+(mismatch&~7u),8);memcpy(&b,payload+(mismatch&~7u),8);
            fprintf(stderr,"DISCONTINUITY sample=%llu byte=%u previous=%016llx current=%016llx late_us=%.1f LA=%08x\n",blocks*STEP,mismatch,a,b,(now()-target)*1e6,rd32(0x7c0));
            for(unsigned j=0;j<32;j++){memcpy(&a,tail+j*8,8);memcpy(&b,payload+j*8,8);if(a!=b)fprintf(stderr,"overlap[%u] %016llx -> %016llx\n",j,a,b);}goto done;
        }
        if(iq_check_freshness(&fresh,payload,STEP,blocks*STEP))goto done;
        memcpy(tail,payload+STEP*8,sizeof tail);
        for(int j=0;j<STEP;j++){uint64_t v;memcpy(&v,payload+j*8,8);iq[2*j]=(int16_t)(la_i(v)*16);iq[2*j+1]=(int16_t)(la_q(v)*16);}
        if(fwrite(iq,sizeof iq,1,file)!=1)goto done;
        blocks++;target+=period;double cost=now()-t;if(cost>maxcost)maxcost=cost;
    }
    result=0;
done:
    printf("%s samples=%llu RF_s=%.6f wall_s=%.6f max_fetch_ms=%.3f\n",result?"FAILED":"OK",blocks*STEP,blocks*STEP/fs,begin?now()-begin:0,maxcost*1000);
    printf("freshness_checks=%u async=%d fetches=%u stages_us submit=%.1f publish=%.1f wait=%.1f max=%.1f/%.1f/%.1f\n",fresh.checks,async,fetches,
        fetches?stages[0]*1e6/fetches:0,fetches?stages[1]*1e6/fetches:0,fetches?stages[2]*1e6/fetches:0,
        stage_max[0]*1e6,stage_max[1]*1e6,stage_max[2]*1e6);
    for(int i=0;i<3;i++)if(controls.transfer[i]){
        if(!controls.wait[i].done){libusb_cancel_transfer(controls.transfer[i]);while(!controls.wait[i].done){struct timeval tv={0,10000};libusb_handle_events_timeout(g_ctx,&tv);}}
        libusb_free_transfer(controls.transfer[i]);
    }
    if(transfer){if(!wait.done){libusb_cancel_transfer(transfer);while(!wait.done){struct timeval tv={0,10000};libusb_handle_events_timeout(g_ctx,&tv);}}libusb_free_transfer(transfer);}
    if(file&&fclose(file))result=1;
    SetThreadPriority(GetCurrentThread(),priority);
    SetConsoleCtrlHandler(on_stop,FALSE);
    if(rtw_power_on()||rtw_phy_init()||rtw_set_channel(11,BW20))result=1;
    rtw_close();return result;
}
