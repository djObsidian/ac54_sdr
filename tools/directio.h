#pragma once
/* Shared experimental direct C2H transport; caller initializes USB and LA. */
struct control_queue {
    struct libusb_transfer *transfer[3];
    struct iq_bulk_wait wait[3];
    unsigned char data[3][LIBUSB_CONTROL_SETUP_SIZE+24];
};
static double stages[3],stage_max[3];static unsigned fetches;
static int guard;
static int queue_write(struct control_queue *q,int i,unsigned reg,const void *data,unsigned bytes) {
    libusb_fill_control_setup(q->data[i],0x40,5,(uint16_t)reg,0,(uint16_t)bytes);
    memcpy(q->data[i]+LIBUSB_CONTROL_SETUP_SIZE,data,bytes);
    libusb_fill_control_transfer(q->transfer[i],g_h,q->data[i],iq_bulk_done,&q->wait[i],100);
    q->wait[i].done=0;
    if(libusb_submit_transfer(q->transfer[i])){q->wait[i].done=1;return -1;}
    return 0;
}
static int direct_fetch(unsigned offset,unsigned bytes,unsigned char *out,
                        struct libusb_transfer *transfer,struct iq_bulk_wait *wait,
                        unsigned char *rx,struct control_queue *q) {
    unsigned addr=offset<0x10000?0x16000+offset:0x6000+offset-0x10000;
    unsigned haddr=addr-24;
    unsigned ocp=haddr<0x6000?0x18700000+haddr:
        haddr<0x16000?0x187b0000+haddr-0x6000:0x187a0000+haddr-0x16000;
    unsigned char header[24]={0};header[0]=bytes&255;header[1]=bytes>>8;header[3]=0x80;header[11]=0x10;
    unsigned char before[280],after[280];
    int guarded=guard&&(ocp&4095)+sizeof before<=4096;
    if(guarded&&(put(0x140,(ocp&0xffffff)>>12,2)||la_blk_rd(0x8000+(ocp&4095),before,sizeof before)!=sizeof before))return -1;
    double t[4];t[0]=now();wait->done=0;
    if(libusb_submit_transfer(transfer)){wait->done=1;return -1;}
    t[1]=now();
    if(q) {
        uint16_t page=(uint16_t)((ocp&0xffffff)>>12);
        uint32_t request=(haddr&65535)|((haddr>>16)<<24)|0x10000;
        /* Same EP0 pipe, submitted in dependency order. Verify all completions. */
        if(queue_write(q,0,0x140,&page,2)||queue_write(q,1,0x8000+(ocp&4095),header,24)||
           queue_write(q,2,0x294,&request,4))return -1;
    }else {
        if(put(0x140,(ocp&0xffffff)>>12,2)||la_blk_wr(0x8000+(ocp&4095),header,24)!=24)return -1;
        if(guarded) {
            if(la_blk_rd(0x8000+(ocp&4095),after,sizeof after)!=sizeof after)return -1;
            if(memcmp(before+24,after+24,256)){fprintf(stderr,"GUARD payload changed during descriptor write offset=%x\n",offset);return -1;}
        }
        if(put(0x294,(haddr&65535)|((haddr>>16)<<24)|0x10000,4))return -1;
    }
    t[2]=now();
    while(!wait->done||(q&&(!q->wait[0].done||!q->wait[1].done||!q->wait[2].done))){
        struct timeval tv={0,10000};if(libusb_handle_events_timeout(g_ctx,&tv))return -1;
    }
    t[3]=now();
    if(guarded) {
        if(la_blk_rd(0x8000+(ocp&4095),after,sizeof after)!=sizeof after)return -1;
        if(memcmp(before+24,after+24,256)){
            fprintf(stderr,"GUARD payload changed during C2H offset=%x bytes=%u page=%x LA=%08x\n",offset,bytes,rd16(0x140),rd32(0x7c0));
            for(unsigned j=24;j<280;j+=8){uint64_t a,b;memcpy(&a,before+j,8);memcpy(&b,after+j,8);if(a!=b)fprintf(stderr,"guard[%u] %016llx -> %016llx\n",(j-24)/8,a,b);}
            return -1;
        }
    }
    if(q)for(int i=0;i<3;i++)if(q->wait[i].status!=LIBUSB_TRANSFER_COMPLETED||q->wait[i].length!=(i==0?2:i==1?24:4)){
        fprintf(stderr,"EP0 failure command=%d status=%d bytes=%d\n",i,q->wait[i].status,q->wait[i].length);return -1;
    }
    if(wait->status!=LIBUSB_TRANSFER_COMPLETED||wait->length!=(int)bytes+24||memcmp(rx,header,12)){
        fprintf(stderr,"direct packet failure offset=%x status=%d length=%d wanted=%u\n",offset,wait->status,wait->length,bytes+24);
        for(int i=0;i<24&&i<wait->length;i++)fprintf(stderr,"%02x%s",rx[i],i==23?"\n":" ");return -1;
    }
    for(int i=0;i<3;i++){double cost=t[i+1]-t[i];stages[i]+=cost;if(cost>stage_max[i])stage_max[i]=cost;}
    fetches++;memcpy(out,rx+24,bytes);return 0;
}
