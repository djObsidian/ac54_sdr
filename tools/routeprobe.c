/* Distinguish SRAM routes by unique, verified markers. No DDMA, no ADC.
 * route=0: C2H at 0x294; route=1: FWFF 0x298/0x29c.
 * Reinitialize before/after each attempt; never replay pending requests.
 */
#define _CRT_SECURE_NO_WARNINGS
#include "rtwinit.h"
#include "la.h"
#include <stdlib.h>
#define MARKER_SIZE 256
#define PKT_SIZE (24+MARKER_SIZE)
static int memio(unsigned page,unsigned char *data,int write) {
    if(wr16(0x140,page)!=2)return -1;
    int n=write?la_blk_wr(0x8000,data,PKT_SIZE):la_blk_rd(0x8000,data,PKT_SIZE);
    return n==PKT_SIZE?0:-1;
}
static void status(const char *tag) {
    printf("%s c2h=%08x fwaddr=%08x fwctrl=%08x fwinfo=%08x rxdma=%08x\n",tag,
        rd32(0x294),rd32(0x298),rd32(0x29c),rd32(0x2a0),rd32(0x288));
}
int main(int argc,char **argv) {
    unsigned route=0,address=0x1000,start=0x1000,num=1;int result=1,initialized=0;
    for(int i=1;i<argc;i++) {
        if(!strncmp(argv[i],"route=",6))route=strtoul(argv[i]+6,NULL,0);
        else if(!strncmp(argv[i],"addr=",5))address=strtoul(argv[i]+5,NULL,0);
        else if(!strncmp(argv[i],"start=",6))start=strtoul(argv[i]+6,NULL,0);
        else if(!strncmp(argv[i],"num=",4))num=strtoul(argv[i]+4,NULL,0);
        else return 1;
    }
    if(route>1||address>(route?0x3ffffu:0xfffffu)||start>0xffff||num>1)return 1;
    unsigned pages[]={0x701,0x681,0x781,0x7a1};const char *names[]={"RX","FW","TX","LA"};
    unsigned char packets[4][PKT_SIZE]={{0}},verify[PKT_SIZE],rx[65536];
    setvbuf(stdout,NULL,_IONBF,0);
    if(rtw_open())return 1;
    initialized=1;
    if(rtw_power_on()||rtw_read_efuse()||rtw_phy_init()||rtw_set_channel(11,BW20))goto done;
    if(wr8(0x7c0,0)!=1||wr16(0x100,(rd16(0x100)|0xa)&~0xc0u)!=2||
       wr32(0x608,rd32(0x608)|0x80000)!=4||wr8(0x290,0xe)!=1||
       wr8(0x10c,rd8(0x10c)&~4u)!=1||wr8(0x106,0x69)!=1)goto done;
    for(int s=0;s<4;s++) {
        unsigned char *p=packets[s];p[1]=1;p[3]=0x80;p[11]=0x10;
        uint32_t random=0x12345678u+0x357ac1u*s;
        for(int j=24;j<PKT_SIZE;j++){random^=random<<13;random^=random>>17;random^=random<<5;p[j]=(unsigned char)random;}
        if(memio(pages[s],p,1))goto done;
    }
    /* Verify after ALL writes, also catching accidental aliases. */
    for(int s=0;s<4;s++)if(memio(pages[s],verify,0)||memcmp(verify,packets[s],PKT_SIZE)){
        fprintf(stderr,"SRAM marker verification failed: %s\n",names[s]);goto done;
    }
    for(int k=0;k<32;k++){int n=0;if(libusb_bulk_transfer(g_h,0x84,rx,sizeof rx,&n,10))break;}
    printf("TEST route=%u addr=%05x start=%04x num=%u (four distinct markers verified)\n",route,address,start,num);
    status("before");
    if(route==0) {
        if(wr32(0x294,(address&0xffff)|((address>>16)<<24)|0x10000)!=4)goto done;
    }else {
        if(wr32(0x298,address)!=4||wr32(0x29c,0x80000000u|(num<<16)|start)!=4)goto done;
    }
    int found=0;ULONGLONG end=GetTickCount64()+200;
    while(GetTickCount64()<end) {
        int n=0,r=libusb_bulk_transfer(g_h,0x84,rx,sizeof rx,&n,30);
        if(n) {
            printf("bulk status=%d bytes=%d\n",r,n);
            for(int s=0;s<4;s++)for(int off=0;off+MARKER_SIZE<=n;off++)
                if(!memcmp(rx+off,packets[s]+24,MARKER_SIZE)){printf("EXACT %s MARKER offset=%d\n",names[s],off);found++;}
        }
        if(r&&r!=LIBUSB_ERROR_TIMEOUT)goto done;
        if(found)break;
    }
    status("after");
    if(!found)puts("NO MARKER");
    if(memio(pages[0],verify,0))goto done;
    for(int s=0;s<4;s++)if(!memcmp(verify,packets[s],PKT_SIZE))printf("RX memory now contains %s packet\n",names[s]);
    result=0; /* A negative routing result is a completed experiment. */
done:
    if(initialized&&(rtw_power_on()||rtw_phy_init()||rtw_set_channel(11,BW20)))result=1;
    printf("probe %s\n",result?"ERROR":"completed");rtw_close();return result;
}
