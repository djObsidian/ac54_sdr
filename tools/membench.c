/* Measure SRAM -> USB independently of ADC pacing and output conversion.
 * Reinitializes the chip before/after, sends and verifies a fixed PRBS block.
 * membench [n=2000] [dma=0|1] [raw=0|1] [bytes=16128] [pad=0] [mode=0|1] [channel=0..5] [fw=0|1] [la=0|1]
 */
#define _CRT_SECURE_NO_WARNINGS
#include "rtwinit.h"
#include "iqstream.h"
typedef int (LIBUSB_CALL *raw_fn)(libusb_device_handle*,uint8_t,int);
static int memwrite(unsigned page,const unsigned char *data,unsigned n)
{
    for(unsigned off=0;off<n;) {
        unsigned size=n-off;if(size>504)size=504;if(size>4096-(off&4095))size=4096-(off&4095);
        if(put(0x140,page+(off>>12),2)||la_blk_wr(0x8000+(off&4095),data+off,(uint16_t)size)!=(int)size)return -1;
        off+=size;
    }return 0;
}
int main(int argc,char **argv)
{
    int count=2000,dma=0,raw=0,bytes=16128,result=1,pad=0,mode=0,channel=0,fw=0,la=0;
    for(int i=1;i<argc;i++) {
        if(sscanf(argv[i],"n=%d",&count)==1)continue;
        if(sscanf(argv[i],"dma=%d",&dma)==1)continue;
        if(sscanf(argv[i],"raw=%d",&raw)==1)continue;
        if(sscanf(argv[i],"bytes=%d",&bytes)==1)continue;
        if(sscanf(argv[i],"pad=%d",&pad)==1)continue;
        if(sscanf(argv[i],"mode=%d",&mode)==1)continue;
        if(sscanf(argv[i],"channel=%d",&channel)==1)continue;
        if(sscanf(argv[i],"fw=%d",&fw)==1)continue;
        if(sscanf(argv[i],"la=%d",&la)==1)continue;
        fprintf(stderr,"usage: membench [n=2000] [dma=0|1] [raw=0|1] [bytes=16128] [pad=0..128, multiple 8] [mode=0|1] [channel=0..5] [fw=0|1] [la=0|1]\n");return 1;
    }
    if(count<1||count>100000||dma<0||dma>1||raw<0||raw>1||bytes<256||bytes>16380||(bytes&3)||pad<0||pad>128||(pad&7))return 1;
    if(mode<0||mode>1||channel<0||channel>5||fw<0||fw>1)return 1;
    if(la<0||la>1||(la&&(fw||dma)))return 1;
    /* Firmware RX FIFO is 12 KiB, with the test packet starting at 4 KiB. */
    if(fw&&bytes+24+pad>8192){fprintf(stderr,"FW SRAM requires bytes+24+pad <= 8192\n");return 1;}
    unsigned char storage[16536]={0},rx[65536];unsigned char *packet=storage+pad;
    struct libusb_transfer *transfer=NULL;struct iq_bulk_wait wait={1,0,0};
    int initialized=0,raw_enabled=0,completed=0;
    double sums[4]={0},maxima[4]={0},begin=0,elapsed=0;
    setvbuf(stdout,NULL,_IONBF,0);
    if(rtw_open())return 1;
    const struct libusb_version *version=libusb_get_version();
    printf("libusb %u.%u.%u.%u\n",version->major,version->minor,version->micro,version->nano);
    raw_fn rawio=(raw_fn)GetProcAddress(GetModuleHandleA("libusb-1.0.dll"),"libusb_endpoint_set_raw_io");
    if(rd8(0xff)!=0x20){fprintf(stderr,"USB3 required\n");goto done;}
    if(rtw_power_on()||rtw_read_efuse()||rtw_phy_init()||rtw_set_channel(11,BW20))goto done;
    initialized=1;
    if(put(0x100,rd16(0x100)&~0x80u,2)||put(0x608,rd32(0x608)|0x80000,4)||put(0x290,0xe,1)||
       put(0x10c,rd8(0x10c)&~4u,1)||put(0x106,0x69,1))goto done;
    packet[0]=bytes&255;packet[1]=bytes>>8;packet[3]=0x80;packet[11]=0x10;
    uint32_t prng=0x45ab123;
    for(int i=24;i<bytes+24;i++){prng^=prng<<13;prng^=prng>>17;prng^=prng<<5;packet[i]=(unsigned char)prng;}
    if(memwrite(0x781,storage,bytes+24+pad))goto done;
    if(la&&memwrite(0x7a1,storage,bytes+24+pad))goto done;
    if(dma)for(int i=24;i<bytes+24;i++)packet[i]^=0xff;
    if(memwrite(fw?0x681:0x701,storage,bytes+24+pad))goto done;
    if(dma)for(int i=24;i<bytes+24;i++)packet[i]^=0xff;
    for(int i=0;i<32;i++){int n=0;if(libusb_bulk_transfer(g_h,0x84,rx,sizeof rx,&n,10))break;}
    if(raw){if(!rawio){fprintf(stderr,"RAW_IO unavailable in DLL\n");goto done;}int e=rawio(g_h,0x84,1);if(e){fprintf(stderr,"RAW_IO: %s\n",libusb_error_name(e));goto done;}raw_enabled=1;}
    transfer=libusb_alloc_transfer(0);if(!transfer)goto done;
    libusb_fill_bulk_transfer(transfer,g_h,0x84,rx,sizeof rx,iq_bulk_done,&wait,100);
    begin=now();
    for(int i=0;i<count;i++) {
        double t[5];t[0]=now();wait.done=0;
        if(libusb_submit_transfer(transfer)){wait.done=1;goto done;}t[1]=now();
        if(dma) {
            unsigned reg=0x1200+16*channel;
            uint32_t command[]={0x18781018+pad,(fw?0x18681018u:0x18701018u)+pad,0x80000000u|((unsigned)mode<<26)|bytes};
            if(la_blk_wr((uint16_t)reg,(unsigned char*)command,sizeof command)!=sizeof command)goto done;
            double deadline=now()+0.01;uint32_t ctrl;
            do {ctrl=rd32(reg+8);}while(ctrl!=0xdeadbeef&&(ctrl&0x80000000)&&now()<deadline);
            if(ctrl==0xdeadbeef||(ctrl&0x80000000))goto done;
        }t[2]=now();
        if(put(fw?0x29c:0x294,(fw?0x80011000u:la?0x01017000u:0x11000u)+pad,4))goto done;t[3]=now();
        while(!wait.done){struct timeval tv={0,10000};if(libusb_handle_events_timeout(g_ctx,&tv))goto done;}t[4]=now();
        if(wait.status!=LIBUSB_TRANSFER_COMPLETED||wait.length!=bytes+24||memcmp(rx,packet,bytes+24)) {
            fprintf(stderr,"data mismatch block=%d status=%d len=%d\n",i,wait.status,wait.length);goto done;
        }
        for(int j=0;j<4;j++){double cost=t[j+1]-t[j];sums[j]+=cost;if(cost>maxima[j])maxima[j]=cost;}
        completed++;
    }result=0;
done:
    if(begin)elapsed=now()-begin;
    if(transfer){if(!wait.done){libusb_cancel_transfer(transfer);while(!wait.done){struct timeval tv={0,10000};libusb_handle_events_timeout(g_ctx,&tv);}}libusb_free_transfer(transfer);}
    if(raw_enabled)rawio(g_h,0x84,0);
    printf("%s la=%d fw=%d dma=%d mode=%d channel=%d raw=%d pad=%d bytes=%d blocks=%d time=%.6f payload_MB/s=%.3f\n",result?"FAILED":"OK",la,fw,dma,mode,channel,raw,pad,bytes,completed,elapsed,elapsed?completed*(double)bytes/elapsed/1e6:0);
    const char *labels[]={"submit","ddma","c2h_control","bulk_wait"};
    for(int j=0;j<4;j++)printf("%s avg=%.1f us max=%.1f us\n",labels[j],completed?sums[j]*1e6/completed:0,maxima[j]*1e6);
    if(initialized && (rtw_power_on()||rtw_phy_init()||rtw_set_channel(11,BW20)))result=1;
    rtw_close();return result;
}
