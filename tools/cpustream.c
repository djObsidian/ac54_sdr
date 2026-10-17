/* CPU schedules opposite-bank C2H using the hardware free-running timer.
 * Every bank: four overlapping C2H chunks + saved prefix/timing trailer.
 * Host emits only complete, fresh, deadline-checked banks. RAM firmware only. */
#define _CRT_SECURE_NO_WARNINGS
#include "rtwinit.h"
#include "iqstream.h"
#include "cpufw.h"
#define QDEPTH 32
#define PREFIX 224
#define FIRST_POS 200
#define CHUNK 16352
#define SHARED 24
#define PARTS 5
struct stream_slot {struct libusb_transfer *x;struct iq_bulk_wait w;unsigned char data[16384];};
static struct stream_slot queue[QDEPTH];
typedef int (LIBUSB_CALL *raw_io_fn)(libusb_device_handle*,uint8_t,int);
static void check_deadline(struct m16 *p) {
    lw16(p,2,1,0);lw16(p,3,0,12);emit(p,0xe26b); /* subu v0,v0,v1 */
    emit(p,0x5200);emit(p,0x6106); /* slti v0,0; btnez good */
    lw16(p,2,1,0);sw16(p,2,0,24);
    emit(p,0x6b01);sw16(p,3,0,16);unsigned stop=p->n;branch(p,0x1000,stop);emit(p,0x6500);
}
static void send_packet(struct m16 *p,unsigned request,unsigned part) {
    lw16(p,2,1,0);sw16(p,2,0,32+part*8);
    li32(p,4,0xb8640294);li32(p,5,request);sw16(p,5,4,0);
    li32(p,6,0x10000);unsigned poll=p->n;lw16(p,2,4,0);emit(p,0xeacc);branch(p,0x2a00,poll);
    lw16(p,2,1,0);sw16(p,2,0,36+part*8);
}
static void bank_program(struct m16 *p,unsigned bank,unsigned period5) {
    /* state: sequence,target,fraction,deadline,error,start,end,remaining */
    lw16(p,2,0,4);addi(p,2,period5/5);lw16(p,3,0,8);addi(p,3,period5%5);
    emit(p,0x5b05);emit(p,0x6104); /* sltiu v1,5; btnez no-carry */
    addi(p,3,-5);addi(p,2,1);
    sw16(p,3,0,8);sw16(p,2,0,4);addi(p,2,(period5*96)/500);sw16(p,2,0,12);
    unsigned wait=p->n;lw16(p,2,1,0);lw16(p,3,0,4);emit(p,0xe26b);emit(p,0x5200);branch(p,0x6100,wait);
    check_deadline(p);lw16(p,2,1,0);sw16(p,2,0,20);
    /* Save prefix before descriptors overwrite it; send with final timestamps. */
    li32(p,4,0xb87a0000+bank*65536);li32(p,5,0xb8701028);
    for(unsigned off=0;off<PREFIX;off+=4){
        if(off&&!(off%128)){addi(p,4,128);addi(p,5,128);}
        lw16(p,2,4,off%128);sw16(p,2,5,off%128);
    }
    unsigned pos=FIRST_POS,part=0;
    while(pos<65536) {
        unsigned size=65536-pos;if(size>CHUNK)size=CHUNK;
        unsigned header=pos-24,addr=(bank?0x6000:0x16000)+header;
        li32(p,4,0xb87a0000+bank*65536+header);li32(p,2,0x80000000|size);sw16(p,2,4,0);
        emit(p,0x6a00);sw16(p,2,4,4);sw16(p,2,4,12);sw16(p,2,4,16);sw16(p,2,4,20);
        li32(p,2,0x10000000);sw16(p,2,4,8);
        send_packet(p,(addr&65535)|((addr>>16)<<24)|0x10000,part++);check_deadline(p);
        pos+=size;if(pos<65536)pos-=SHARED;
    }
    lw16(p,2,1,0);sw16(p,2,0,24);
    li32(p,4,0xb8701018);lw16(p,2,0,0);sw16(p,2,4,0);lw16(p,2,0,20);sw16(p,2,4,4);
    lw16(p,2,0,24);sw16(p,2,4,8);lw16(p,2,0,12);sw16(p,2,4,12);
    send_packet(p,0x11000,part);
    lw16(p,2,0,0);emit(p,0x4a01);sw16(p,2,0,0);lw16(p,2,0,28);emit(p,0x4aff);sw16(p,2,0,28);
}
static void header(unsigned char *p,unsigned len){memset(p,0,24);p[0]=len&255;p[1]=len>>8;p[3]=0x80;p[11]=0x10;}
int main(int argc,char **argv) {
    int rate=argc>1?atoi(argv[1]):5,seconds=argc>2?atoi(argv[2]):3,result=1;
    const char *path=argc>3?argv[3]:"NUL";
    int nocck=argc>4?atoi(argv[4]):0;if(nocck<0||nocck>1)return 1;
    int raw=argc>5?atoi(argv[5]):1,raw_enabled=0;raw_io_fn rawio=NULL;
    int nolpm=argc>6?atoi(argv[6]):0;unsigned char usb_status[2]={0};int lpm_saved=0;
    if(rate<1||rate>6||seconds<1||seconds>60)return 1;
    double fs=80000000.0/(1u<<rate),start=0;unsigned period5=1024u<<(rate-1);
    unsigned wanted=(unsigned)(seconds*fs/16384)*2,complete=0,overlaps=0,max_us=0;
    struct m16 p={0};struct iq_freshness fresh={0};FILE *file=NULL;
    static unsigned char bankbuf[65536];static int16_t iq[16384];
    li32(&p,0,0x80201000);li32(&p,1,0xb8640568);
    emit(&p,0x6a00);for(unsigned off=0;off<28;off+=4)sw16(&p,2,0,off);
    li32(&p,2,wanted);sw16(&p,2,0,28);
    li32(&p,4,0xb86407c0);emit(&p,0x6bcb);emit(&p,0xc460); /* sb v1,0(a0): arm ADC */
    lw16(&p,2,1,0);addi(&p,2,period5/250);sw16(&p,2,0,4);
    if(p.n&1)emit(&p,0x6500);unsigned loop=p.n;
    bank_program(&p,0,period5);bank_program(&p,1,period5);
    lw16(&p,2,0,28);emit(&p,0x2203);jump16(&p,loop); /* beqz v0,done */
    li32(&p,4,0xb86407c0);emit(&p,0x6b00);emit(&p,0xc460);unsigned end=p.n;branch(&p,0x1000,end);
    printf("Firmware halfwords=%u banks=%u rate=%.0f\n",p.n,wanted,fs);
    setvbuf(stdout,NULL,_IONBF,0);SetConsoleCtrlHandler(on_stop,TRUE);if(rtw_open())return 1;
    SetPriorityClass(GetCurrentProcess(),HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_HIGHEST);
    if(raw){rawio=(raw_io_fn)GetProcAddress(GetModuleHandleA("libusb-1.0.dll"),"libusb_endpoint_set_raw_io");
        if(!rawio||rawio(g_h,0x84,1)){fprintf(stderr,"RAW_IO unavailable\n");rtw_close();return 1;}raw_enabled=1;}
    if(rtw_power_on()||rtw_read_efuse()||rtw_phy_init()||rtw_set_channel(11,BW20)||cpu_load(p.code,p.n*2))goto done;
    if(put(0x100,rd16(0x100)|0x8a,2)||put(0x608,(rd32(0x608)|0x80000)&~0xffffu,4)||
       put(0x6a0,0,2)||put(0x6a2,0,2)||put(0x6a4,0,2)||put(0x290,14,1)||put(0x10c,rd8(0x10c)&~4u,1)||
       put(0x7c0,0,1)||put(0x8f8,rd32(0x8f8)&~0x3c00000u,4)||put(0x8b4,rd32(0x8b4)|0x80,4)||
       put(0x95c,(rd32(0x95c)&~0x80000fffu)|((unsigned)rate<<5)|3,4)||put(0x8fc,0,4))goto done;
    if(nocck&&put(0x808,rd32(0x808)&~(1u<<28),4))goto done;
    unsigned char hdr[24];header(hdr,PREFIX+16);if(cpu_mem(0x18701000,hdr,24,1))goto done;
    header(hdr,16);if(cpu_mem(0x18702000,hdr,24,1))goto done;
    for(int i=0;i<32;i++){int n;if(libusb_bulk_transfer(g_h,0x84,queue[0].data,sizeof queue[0].data,&n,10))break;}
    if(nolpm){
        int r=libusb_control_transfer(g_h,0x80,0,0,0,usb_status,2,1000);
        if(r==2){lpm_saved=1;printf("USB status before=%02x%02x\n",usb_status[1],usb_status[0]);}
        for(unsigned feature=48;feature<=49;feature++)printf("CLEAR USB feature %u: %d\n",feature,libusb_control_transfer(g_h,0,1,feature,0,NULL,0,1000));
        unsigned char status[2]={0};r=libusb_control_transfer(g_h,0x80,0,0,0,status,2,1000);
        printf("USB status after=%02x%02x result=%d\n",status[1],status[0],r);
    }
    file=fopen(path,"wb");if(!file)goto done;
    unsigned submitted=0,total=wanted*PARTS;
    for(int i=0;i<QDEPTH;i++){
        queue[i].w.done=1;queue[i].x=libusb_alloc_transfer(0);if(!queue[i].x)goto done;
        libusb_fill_bulk_transfer(queue[i].x,g_h,0x84,queue[i].data,sizeof queue[i].data,iq_bulk_done,&queue[i].w,1000);
        queue[i].w.done=0;if(libusb_submit_transfer(queue[i].x)){queue[i].w.done=1;goto done;}submitted++;
    }
    start=now();if(cpu_start())goto done;
    unsigned packet_no=0,pos=FIRST_POS,available=FIRST_POS;
    while(complete<wanted&&!InterlockedCompareExchange(&stopped,0,0)) {
        struct stream_slot *s=&queue[packet_no%QDEPTH];
        while(!s->w.done){struct timeval tv={0,10000};if(libusb_handle_events_timeout(g_ctx,&tv))goto done;}
        if(s->w.status!=LIBUSB_TRANSFER_COMPLETED){fprintf(stderr,"USB status=%d bank=%u packet=%u\n",s->w.status,complete,packet_no%PARTS);goto done;}
        unsigned type=packet_no%PARTS,size=type==PARTS-1?PREFIX+16:65536-pos;if(type<PARTS-1&&size>CHUNK)size=CHUNK;
        header(hdr,size);
        if(s->w.length!=size+24||memcmp(hdr,s->data,12)){fprintf(stderr,"Packet malformed bank=%u part=%u length=%d want=%u\n",complete,type,s->w.length,size+24);goto done;}
        unsigned char *data=s->data+24;
        if(type<PARTS-1){
            if(memcmp(bankbuf+pos,data,available-pos)){fprintf(stderr,"Overlap failed bank=%u pos=%u\n",complete,pos);goto done;}overlaps++;
            memcpy(bankbuf+pos,data,size);available=pos+size;pos=available==65536?65536:available-SHARED;
        }else {
            uint32_t stamp[4];memcpy(stamp,data,16);
            if(stamp[0]!=complete||(int32_t)(stamp[2]-stamp[3])>=0||available!=65536){fprintf(stderr,"Trailer failed bank=%u\n",complete);goto done;}
            if(memcmp(bankbuf+FIRST_POS,data+16+FIRST_POS,PREFIX-FIRST_POS)){fprintf(stderr,"Prefix overlap failed bank=%u\n",complete);goto done;}
            memcpy(bankbuf,data+16,PREFIX);pos=FIRST_POS;available=FIRST_POS;
            if(stamp[2]-stamp[1]>max_us)max_us=stamp[2]-stamp[1];
            if(iq_check_freshness(&fresh,bankbuf,8192,(unsigned long long)complete*8192)){
                FILE *bad=fopen("cap/cpu-frozen.bin","wb");if(bad){fwrite(bankbuf,1,sizeof bankbuf,bad);fclose(bad);}goto done;
            }
            for(unsigned j=0;j<8192;j++){uint64_t v;memcpy(&v,bankbuf+j*8,8);iq[j*2]=(int16_t)(la_i(v)*16);iq[j*2+1]=(int16_t)(la_q(v)*16);}
            if(fwrite(iq,sizeof iq,1,file)!=1)goto done;complete++;
        }
        packet_no++;
        if(submitted<total){s->w.done=0;if(libusb_submit_transfer(s->x)){s->w.done=1;goto done;}submitted++;}
    }
    result=complete==wanted?0:1;
done:
    cpu_halt();double elapsed=start?now()-start:0;
    for(int i=0;i<QDEPTH;i++)if(queue[i].x){if(!queue[i].w.done){libusb_cancel_transfer(queue[i].x);while(!queue[i].w.done){struct timeval tv={0,10000};libusb_handle_events_timeout(g_ctx,&tv);}}libusb_free_transfer(queue[i].x);}
    uint32_t state[18]={0};if(!copy(0x201000,0x18705000,sizeof state))cpu_mem(0x18705000,(unsigned char*)state,sizeof state,0);
    if(raw_enabled)rawio(g_h,0x84,0);
    if(lpm_saved)for(unsigned bit=2;bit<=3;bit++)if(usb_status[0]&(1u<<bit))libusb_control_transfer(g_h,0,3,46+bit,0,NULL,0,1000);
    printf("CPU state seq=%u target=%u frac=%u deadline=%u error=%u start=%u end=%u remaining=%u\n",state[0],state[1],state[2],state[3],state[4],state[5],state[6],state[7]);
    for(unsigned i=0;i<5;i++)printf("packet %u begin=%u end=%u duration=%u us\n",i,state[8+i*2],state[9+i*2],state[9+i*2]-state[8+i*2]);
    printf("%s samples=%llu RF_s=%.6f wall_s=%.6f max_bank_us=%u overlaps=%u freshness=%u\n",result?"FAILED":"OK",(unsigned long long)complete*8192,complete*8192/fs,elapsed,max_us,overlaps,fresh.checks);
    if(file&&fclose(file))result=1;SetConsoleCtrlHandler(on_stop,FALSE);
    if(put(0x80,0,2)||rtw_power_on()||rtw_phy_init()||rtw_set_channel(11,BW20))result=1;rtw_close();return result;
}
