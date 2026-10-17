/* Test whether 7cc bit30 extends the 8822B LA ring. This bit is experimental
 * on 8822B: vendor FULL_BUFF_MODE_SUPPORT does NOT list this chip.
 * Distinct poison in all TX SRAM; compare which 64 KiB banks ADC overwrites.
 * Stop LA and restore the register/reinitialize on completion. */
#define _CRT_SECURE_NO_WARNINGS
#include "rtwinit.h"
#include "la.h"
int main(int argc,char **argv) {
    int full=0,result=1,saved=0;uint32_t old=0;
    if(argc>2||(argc==2&&sscanf(argv[1],"full=%d",&full)!=1)||full<0||full>1)return 1;
    static unsigned char before[0x40000],after[0x40000];
    setvbuf(stdout,NULL,_IONBF,0);if(rtw_open())return 1;
    if(rtw_power_on()||rtw_read_efuse()||rtw_phy_init()||rtw_set_channel(11,BW20))goto done;
    old=rd32(0x7cc);if(old==0xdeadbeef)goto done;saved=1;
    if(wr8(0x7c0,0)!=1||wr32(0x7cc,(old&~(1u<<30))|((unsigned)full<<30))!=4)goto done;
    /* Keep the clock ungated, as in faststream. MAC RX must stay enabled. */
    if(wr32(0x608,rd32(0x608)|0x80000)!=4)goto done;
    printf("full=%d 7cc old=%08x readback=%08x\n",full,old,rd32(0x7cc));
    uint32_t random=0xe1234567;
    for(unsigned i=0;i<sizeof before;i++){random^=random<<13;random^=random>>17;random^=random<<5;before[i]=(unsigned char)random;}
    if(txff_xfer(0,before,sizeof before,1)||txff_xfer(0,after,sizeof after,0)||memcmp(before,after,sizeof before))goto done;
    if(wr32(0x8f8,rd32(0x8f8)&~0x3c00000u)!=4||wr32(0x8b4,rd32(0x8b4)|0x80)!=4||
       wr32(0x95c,(rd32(0x95c)&~0x80000fffu)|(6u<<5)|3)!=4||wr32(0x8fc,0)!=4||wr8(0x7c0,0xcb)!=1)goto done;
    Sleep(60);printf("armed=%08x\n",rd32(0x7c0));if(wr8(0x7c0,0xeb)!=1)goto done;
    for(int i=0;i<100&&(rd32(0x7c0)&2);i++)Sleep(1);
    printf("triggered=%08x\n",rd32(0x7c0));if(rd32(0x7c0)&2)goto done;
    if(txff_xfer(0,after,sizeof after,0))goto done;
    unsigned upper_changes=0;
    for(unsigned bank=0;bank<4;bank++){
        unsigned changes=0;for(unsigned j=0;j<65536;j++)changes+=before[bank*65536+j]!=after[bank*65536+j];
        printf("TX=%05x changed_bytes=%u/65536\n",bank*65536,changes);
        if(bank>=2)upper_changes+=changes;
    }
    if(upper_changes<100000){fprintf(stderr,"Capture positive control failed; result inconclusive\n");goto done;}
    result=0;
done:
    wr8(0x7c0,0);if(saved&&wr32(0x7cc,old)!=4)result=1;
    if(rtw_power_on()||rtw_phy_init()||rtw_set_channel(11,BW20))result=1;
    printf("ringprobe %s\n",result?"FAILED":"OK");rtw_close();return result;
}
