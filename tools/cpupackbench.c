/* Measured CPU packing upper bound: discard upper 32 of each 64-bit LA word.
 * Static PRBS, verified result, hardware timer; NOT live ADC throughput. */
#define _CRT_SECURE_NO_WARNINGS
#include "rtwinit.h"
#include "iqstream.h"
#include "cpufw.h"
int main(int argc,char **argv) {
    int clocksel=argc>1?atoi(argv[1]):0,result=1;if(clocksel<0||clocksel>3)return 1;
    const unsigned repeats=2000,samples=256;
    struct m16 p={0};unsigned char source[2048],expected[1024],actual[1024];uint32_t status[4]={0};
    li32(&p,0,0xb8705800);li32(&p,1,0xb8640568);li32(&p,7,repeats);
    lw16(&p,2,1,0);sw16(&p,2,0,4);
    unsigned outer=p.n;li32(&p,4,0xb87a1000);li32(&p,5,0xb8701000);emit(&p,0x6e20);
    unsigned inner=p.n;
    for(unsigned i=0;i<8;i++){lw16(&p,2,4,i*8);sw16(&p,2,5,i*4);}
    emit(&p,0x4c40);emit(&p,0x4d20);emit(&p,0x4eff);branch(&p,0x2e00,inner);
    emit(&p,0x4fff);branch(&p,0x2f00,outer);lw16(&p,2,1,0);sw16(&p,2,0,8);
    emit(&p,0x6a01);sw16(&p,2,0,0);unsigned end=p.n;branch(&p,0x1000,end);
    uint32_t rng=0x815237ab;
    for(unsigned i=0;i<sizeof source;i++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;source[i]=(unsigned char)rng;}
    for(unsigned i=0;i<samples;i++)memcpy(expected+i*4,source+i*8,4);
    setvbuf(stdout,NULL,_IONBF,0);if(rtw_open())return 1;
    if(rtw_power_on()||rtw_read_efuse()||rtw_phy_init()||rtw_set_channel(11,BW20)||
       put(0x100,rd16(0x100)&~0xc0u,2)||put(0x80,(rd16(0x80)&~0x3000u)|(clocksel<<12),2)||cpu_load(p.code,p.n*2)||
       cpu_mem(0x187a1000,source,sizeof source,1)||cpu_mem(0x18705800,(unsigned char*)status,sizeof status,1)||cpu_start())goto done;
    double limit=now()+3;
    do{Sleep(1);if(cpu_mem(0x18705800,(unsigned char*)status,sizeof status,0))goto done;}while(!status[0]&&now()<limit);
    if(status[0]!=1||cpu_mem(0x18701000,actual,sizeof actual,0)||memcmp(expected,actual,sizeof actual))goto done;
    unsigned us=status[2]-status[1];
    printf("PACK VERIFIED selector=%d samples=%u us=%u MSPS=%.3f packed_MBps=%.3f\n",clocksel,repeats*samples,us,(double)repeats*samples/us,(double)repeats*samples*4/us);result=0;
done:
    cpu_halt();if(put(0x80,0,2)||rtw_power_on()||rtw_phy_init()||rtw_set_channel(11,BW20))result=1;
    printf("pack probe %s status=%08x\n",result?"FAILED":"OK",status[0]);rtw_close();return result;
}
