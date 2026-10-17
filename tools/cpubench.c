/* CPU-driven C2H throughput, verified static PRBS. This is NOT live ADC. */
#define _CRT_SECURE_NO_WARNINGS
#include "rtwinit.h"
#include "iqstream.h"
#include "cpufw.h"
#define DEPTH 8
struct slot {struct libusb_transfer *x;struct iq_bulk_wait w;unsigned char data[65536];};
static struct slot slots[DEPTH];
typedef int (LIBUSB_CALL *raw_io_fn)(libusb_device_handle*,uint8_t,int);
int main(int argc,char **argv) {
    int count=argc>1?atoi(argv[1]):2000,result=1,complete=0,bytes=argc>2?atoi(argv[2]):PAYLOAD;double start=0,elapsed=0;
    if(count<DEPTH||count>100000||bytes<256||bytes>16376||(bytes&7))return 1;
    unsigned packet_size=bytes+24;
    int raw=argc>3?atoi(argv[3]):0,raw_enabled=0;raw_io_fn rawio=NULL;
    int agg=argc>4?atoi(argv[4]):0;if(agg<0||agg>2)return 1;
    static unsigned char packet[16400];uint32_t rng=0x67a72b31;
    struct m16 p={0};
    li32(&p,4,0xb8640294); /* a0 = C2H request register */
    li32(&p,5,0x00011000); /* a1 = one packet at RX 0x1000 */
    li32(&p,6,0x00010000); /* a2 = busy bit */
    li32(&p,7,0xb8705800); /* a3 = progress in separate RX SRAM */
    li32(&p,0,count);emit(&p,0x6900); /* s0 remaining; s1 completed */
    unsigned loop=p.n;emit(&p,0xdca0); /* sw a1,0(a0) */
    unsigned poll=p.n;emit(&p,0x9c40);emit(&p,0xeacc); /* lw v0,0(a0); and v0,a2 */
    branch(&p,0x2a00,poll); /* bnez v0,poll */
    emit(&p,0x4901);emit(&p,0xdf20); /* addiu s1,1; sw s1,0(a3) */
    emit(&p,0x48ff);branch(&p,0x2800,loop); /* addiu s0,-1; bnez s0,loop */
    unsigned end=p.n;branch(&p,0x1000,end);
    setvbuf(stdout,NULL,_IONBF,0);if(rtw_open())return 1;
    if(raw){rawio=(raw_io_fn)GetProcAddress(GetModuleHandleA("libusb-1.0.dll"),"libusb_endpoint_set_raw_io");
        if(!rawio||rawio(g_h,0x84,1)){fprintf(stderr,"RAW_IO unavailable\n");rtw_close();return 1;}raw_enabled=1;}
    if(rtw_power_on()||rtw_read_efuse()||rtw_phy_init()||rtw_set_channel(11,BW20))goto done;
    if(put(0x100,rd16(0x100)&~0xc0u,2)||put(0x290,14,1)||put(0x10c,rd8(0x10c)&~4u,1)||cpu_load(p.code,p.n*2))goto done;
    if(agg&&(put(0x280,0x20000a05u|(agg==2?0x80000000u:0),4)||put(0x10c,rd8(0x10c)|4u,1)))goto done;
    packet[0]=bytes&255;packet[1]=bytes>>8;packet[3]=0x80;packet[11]=0x10;
    for(unsigned i=24;i<packet_size;i++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;packet[i]=(unsigned char)rng;}
    uint32_t zero=0;
    if(cpu_mem(0x18701000,packet,packet_size,1)||cpu_mem(0x18705800,(unsigned char*)&zero,4,1))goto done;
    for(int i=0;i<32;i++){int n;if(libusb_bulk_transfer(g_h,0x84,slots[0].data,sizeof slots[0].data,&n,10))break;}
    for(int i=0;i<DEPTH;i++){
        slots[i].w.done=1;slots[i].x=libusb_alloc_transfer(0);if(!slots[i].x)goto done;
        libusb_fill_bulk_transfer(slots[i].x,g_h,0x84,slots[i].data,sizeof slots[i].data,iq_bulk_done,&slots[i].w,1000);
        slots[i].w.done=0;if(libusb_submit_transfer(slots[i].x)){slots[i].w.done=1;goto done;}
    }
    start=now();if(cpu_start())goto done;
    int submitted=DEPTH;
    while(complete<count){
        struct timeval tv={0,10000};if(libusb_handle_events_timeout(g_ctx,&tv))goto done;
        for(int i=0;i<DEPTH;i++)if(slots[i].w.done){
            struct slot *s=&slots[i];if(!s->x)continue;
            if(s->w.status!=LIBUSB_TRANSFER_COMPLETED||s->w.length!=packet_size||memcmp(s->data,packet,packet_size)){
                fprintf(stderr,"CPU bulk mismatch complete=%d slot=%d status=%d bytes=%d\n",complete,i,s->w.status,s->w.length);goto done;}
            complete++;
            if(submitted<count){s->w.done=0;if(libusb_submit_transfer(s->x)){s->w.done=1;goto done;}submitted++;}
            else {libusb_free_transfer(s->x);s->x=NULL;}
        }
    }result=0;
done:
    if(start)elapsed=now()-start;cpu_halt();
    for(int i=0;i<DEPTH;i++)if(slots[i].x){if(!slots[i].w.done){libusb_cancel_transfer(slots[i].x);while(!slots[i].w.done){struct timeval tv={0,10000};libusb_handle_events_timeout(g_ctx,&tv);}}libusb_free_transfer(slots[i].x);}
    if(raw_enabled)rawio(g_h,0x84,0);
    uint32_t progress=0;cpu_mem(0x18705800,(unsigned char*)&progress,4,0);
    printf("%s bytes=%d verified=%d CPU_progress=%u seconds=%.6f payload_MBps=%.3f\n",result?"FAILED":"OK",bytes,complete,progress,elapsed,elapsed?complete*(double)bytes/elapsed/1e6:0);
    if(put(0x80,0,2)||rtw_power_on()||rtw_phy_init()||rtw_set_channel(11,BW20))result=1;
    rtw_close();return result;
}
