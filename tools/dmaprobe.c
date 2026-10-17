/* SRAM -> C2H bulk routing experiment for RTL8822BU. No firmware/efuse writes.
 * dmaprobe [space=tx|rx|fw] [addr=byte-address] [ddma=0|1]
 * Backs up 280 SRAM bytes at offset 0x1000, injects an RX descriptor + marker,
 * optionally copies TX to the selected space via DDMA, asks C2H to send it.
 * Address units/routing are under test, so success requires exact marker match.
 */
#define _CRT_SECURE_NO_WARNINGS
#include "rtw.h"
#include <stdlib.h>
#define SIZE 280
static int memio(unsigned page, unsigned char *p, int write)
{
    if (wr16(0x140,page)!=2) return -1;
    int r=libusb_control_transfer(g_h,write?0x40:0xc0,5,0x8000,0,p,SIZE,1000);
    return r==SIZE?0:-1;
}
static int put(unsigned a,unsigned v,int n) {return wr(a,v,n)==n?0:-1;}
int main(int argc,char **argv)
{
    unsigned base=0x18700000, page=0x701, address=0x1000;
    int dma=1, result=1, txsaved=0, dstsaved=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"space=tx")) {base=0x18780000;page=0x781;continue;}
        if(!strcmp(argv[i],"space=rx")) {base=0x18700000;page=0x701;continue;}
        if(!strcmp(argv[i],"space=fw")) {base=0x18680000;page=0x681;continue;}
        if(!strncmp(argv[i],"addr=",5)) {address=strtoul(argv[i]+5,NULL,0);continue;}
        if(!strncmp(argv[i],"ddma=",5)) {dma=atoi(argv[i]+5);continue;}
        fprintf(stderr,"usage: dmaprobe [space=rx|tx|fw] [addr=0x1000] [ddma=0|1]\n");return 1;
    }
    if(address>0xfffff || dma<0 || dma>1) return 1;
    setvbuf(stdout,NULL,_IONBF,0);
    if(rtw_open())return 1;
    const unsigned regs[]={0x100,0x608,0x290,0x10c,0x106,0x140,0x294,0x1080,0x1200,0x1204,0x1208};
    const int widths[]={2,4,1,1,1,2,4,4,4,4,4};
    unsigned backup[11];int saved=0;
    unsigned char packet[SIZE]={0}, oldtx[SIZE], olddst[SIZE], verify[SIZE], rx[65536];
    for(;saved<11;saved++) {backup[saved]=rd(regs[saved],widths[saved]);if(backup[saved]==0xdeadbeef)goto done;}
    if(backup[10]&0x80000000 || backup[6]&0x10000) {puts("DMA/C2H busy; initialize chip first");goto done;}
    /* MAC RX off; HCI RX DMA on. Keep ordinary RF frames out of the test. */
    if(put(0x100,(backup[0]|0x0a)&~0x80u,2)||put(0x608,backup[1]|0x80000,4)||
       put(0x290,rd8(0xff)==0x20?0x0e:0x1e,1)||put(0x10c,backup[3]&~4u,1)||put(0x106,0x69,1))goto done;
    if(memio(0x781,oldtx,0))goto done;txsaved=1;
    if(page!=0x781) {if(memio(page,olddst,0))goto done;dstsaved=1;}
    /* RX descriptor: payload length 256, OWN, C2H flag; marker is deterministic. */
    packet[1]=1;packet[3]=0x80;packet[11]=0x10;
    for(int i=24;i<SIZE;i++)packet[i]=(unsigned char)((i-24)*73+19);
    if(memio(0x781,packet,1))goto done;
    if(dma) {
        if(put(0x1080,backup[7]|0x10100,4)||put(0x1200,0x18781000,4)||
           put(0x1204,base+0x1000,4)||put(0x1208,0x80000000|SIZE,4))goto done;
        for(int i=0;i<100 && (rd32(0x1208)&0x80000000);i++)Sleep(1);
        printf("DDMA ctrl=%08x\n",rd32(0x1208));
        if(rd32(0x1208)&0x80000000)goto done;
    } else if(memio(page,packet,1))goto done;
    if(memio(page,verify,0)||memcmp(packet,verify,SIZE)) {puts("SRAM verification failed");goto done;}
    printf("SRAM verified page=%03x; requesting C2H addr=%05x\n",page,address);
    /* Drain any pre-existing RX traffic before publishing the marker. */
    int n=0;
    for(int i=0;i<32;i++)if(libusb_bulk_transfer(g_h,0x84,rx,sizeof rx,&n,10))break;
    unsigned request=(address&0xffff)|((address>>16)<<24)|0x10000;
    if(put(0x294,request,4))goto done;
    ULONGLONG end=GetTickCount64()+1000;
    while(GetTickCount64()<end) {
        n=0;int r=libusb_bulk_transfer(g_h,0x84,rx,sizeof rx,&n,100);
        if(n) {
            printf("bulk r=%d len=%d first=",r,n);
            for(int i=0;i<n && i<32;i++)printf("%02x",rx[i]);puts("");
            for(int i=0;i+256<=n;i++)if(!memcmp(rx+i,packet+24,256)) {puts("EXACT MARKER RECEIVED");result=0;goto done;}
        }
        if(r && r!=LIBUSB_ERROR_TIMEOUT)break;
    }
    printf("No marker; C2H=%08x RXDMA=%08x\n",rd32(0x294),rd32(0x288));
done:
    /* A pending request cannot safely be replayed while restoring memory. */
    if(saved==11)wr32(0x294,backup[6]&~0x10000u);
    if(dstsaved && memio(page,olddst,1))result=1;
    if(txsaved && memio(0x781,oldtx,1))result=1;
    while(saved--)if(put(regs[saved],backup[saved],widths[saved]))result=1;
    rtw_close();return result;
}
