/* Verify all 128 KiB LA SRAM through the proposed direct C2H alias.
 * Every page has a distinct PRBS packet. No DDMA or live ADC. */
#define _CRT_SECURE_NO_WARNINGS
#include "rtwinit.h"
#include "la.h"
int main(int argc,char **argv) {
    int enabled=0;
    if(argc>2||(argc==2&&sscanf(argv[1],"enabled=%d",&enabled)!=1)||enabled<0||enabled>1)return 1;
    static unsigned char expected[LA_SIZE],rx[65536];int result=1;
    setvbuf(stdout,NULL,_IONBF,0);if(rtw_open())return 1;
    if(rtw_power_on()||rtw_read_efuse()||rtw_phy_init()||rtw_set_channel(11,BW20))goto done;
    if(wr8(0x7c0,enabled?1:0)!=1||wr16(0x100,(rd16(0x100)|0xa)&~0xc0u)!=2||
       wr32(0x608,rd32(0x608)|0x80000)!=4||wr8(0x290,0xe)!=1||wr8(0x10c,rd8(0x10c)&~4u)!=1)goto done;
    uint32_t random=0x972546ab;
    for(unsigned off=0;off<LA_SIZE;off+=4096) {
        unsigned char *p=expected+off;unsigned bytes=4096-24;
        p[0]=bytes&255;p[1]=bytes>>8;p[3]=0x80;p[11]=0x10;
        for(unsigned j=24;j<4096;j++){random^=random<<13;random^=random>>17;random^=random<<5;p[j]=(unsigned char)random;}
    }
    if(txff_xfer(LA_START,expected,LA_SIZE,1))goto done;
    for(int k=0;k<32;k++){int n=0;if(libusb_bulk_transfer(g_h,0x84,rx,sizeof rx,&n,10))break;}
    printf("LA enabled=%d\n",enabled);unsigned found=0;
    for(unsigned address=0;address<0x40000;address+=4096) {
        if(wr32(0x294,(address&65535)|((address>>16)<<24)|0x10000)!=4)goto done;
        int n=0,r=libusb_bulk_transfer(g_h,0x84,rx,sizeof rx,&n,100);
        int matched=0;
        if(!r&&n==4096)for(unsigned off=0;off<LA_SIZE;off+=4096)
            if(!memcmp(rx,expected+off,4096)){
                printf("EXACT TX=%05x C2H=%05x bytes=4096\n",LA_START+off,address);
                found|=1u<<(off/4096);matched=1;
            }
        if(!matched)printf("OTHER C2H=%05x status=%d length=%d\n",address,r,n);
        if(r)goto done;
    }
    printf("found_mask=%08x\n",found);result=found==0xffffffffu?0:1;
done:
    if(rtw_power_on()||rtw_phy_init()||rtw_set_channel(11,BW20))result=1;
    printf("LA map %s\n",result?"FAILED":"OK");rtw_close();return result;
}
