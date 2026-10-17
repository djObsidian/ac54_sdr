/* Verified memory-only DDMA matrix. No live ADC or USB bulk throughput claim.
 * n=20; disjoint ranges, respecting the 24 KiB RX SRAM limit.
 * Every copy gets fresh PRBS source and inverse destination, verified outside
 * the timed interval. Reinitializes hardware before and after the experiment.
 */
#define _CRT_SECURE_NO_WARNINGS
#include "rtwinit.h"
#include "la.h"
static double seconds(void) {
    LARGE_INTEGER t,f; QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f);
    return (double)t.QuadPart/f.QuadPart;
}
static int memory(unsigned address,unsigned char *data,unsigned size,int write) {
    unsigned page=~0u;
    while(size) {
        unsigned off=address&4095,n=4096-off; if(n>504)n=504;if(n>size)n=size;
        unsigned p=(address&0xffffff)>>12;
        if(p!=page){if(wr16(0x140,p)!=2)return -1;page=p;}
        int r=write?la_blk_wr((uint16_t)(0x8000+off),data,(uint16_t)n):la_blk_rd((uint16_t)(0x8000+off),data,(uint16_t)n);
        if(r!=(int)n)return -1;
        address+=n;data+=n;size-=n;
    }return 0;
}
int main(int argc,char **argv) {
    int count=20,result=1,initialized=0,cpu=-1,debug=1,gap=0,reset=0;uint32_t oldclock=0;int clock_saved=0;
    for(int i=1;i<argc;i++) {
        if(sscanf(argv[i],"n=%d",&count)==1)continue;
        if(sscanf(argv[i],"cpu=%d",&cpu)==1)continue;
        if(sscanf(argv[i],"debug=%d",&debug)==1)continue;
        if(sscanf(argv[i],"gap=%d",&gap)==1)continue;
        if(sscanf(argv[i],"reset=%d",&reset)==1)continue;
        fprintf(stderr,"usage: ddmabench [n=1..1000] [cpu=0..3] [debug=0|1] [gap=0..2000 us] [reset=0|1]\n");return 1;
    }
    if(count<1||count>1000||cpu< -1||cpu>3||debug<0||debug>1||gap<0||gap>2000||reset<0||reset>1)return 1;
    const unsigned bases[]={0x18700000,0x18780000,0x187a0000};
    const char *names[]={"RX","TX","LA"};
    const unsigned sizes[]={256,4096,8192,16128};
    unsigned char source[16128],poison[16128],verify[16128];uint32_t random=0x654321ab;
    setvbuf(stdout,NULL,_IONBF,0);
    if(rtw_open())return 1;
    initialized=1;
    if(rtw_power_on()||rtw_read_efuse()||rtw_phy_init()||rtw_set_channel(11,BW20))goto done;
    if(wr8(0x7c0,0)!=1||wr16(0x100,rd16(0x100)&~0xc0u)!=2||wr8(0x106,0x69)!=1)goto done;
    /* Vendor halmac_fw_88xx.c: pltfm_reset_88xx, 8822B clock sync fix. */
    if(reset) {
        uint32_t platform=rd8(0x1082),clk=rd8(9);
        if(platform==0xdeadbeef||clk==0xdeadbeef)goto done;
        if(wr8(0x1082,platform&~1u)!=1||wr8(9,clk&~0x40u)!=1||
           wr8(0x1082,platform|1)!=1||wr8(9,clk|0x40)!=1)goto done;
    }
    /* HALMAC explicitly defines MCUFW_CTRL[13:12] for 8822B.
     * Selector encodings are experimental; do not label them as MHz. */
    oldclock=rd32(0x80);if(oldclock==0xdeadbeef)goto done;clock_saved=1;
    if(cpu>=0) {
        if(wr32(0x80,(oldclock&~0x3000u)|((unsigned)cpu<<12))!=4)goto done;
        uint32_t actual=rd32(0x80);
        if((actual&0x3000u)!=((unsigned)cpu<<12)){fprintf(stderr,"CPU selector did not stick: requested=%d readback=%08x\n",cpu,actual);goto done;}
    }
    printf("REGS 0008=%08x 0080=%08x 0100=%08x 1080=%08x 1100=%08x\n",rd32(8),rd32(0x80),rd32(0x100),rd32(0x1080),rd32(0x1100));
    printf("CONFIG cpu=%d debug=%d gap_us=%d reset=%d\n",cpu,debug,gap,reset);
    puts("src,dst,bytes,copies,avg_us,min_us,max_us,MBps,polls_avg");
    for(int s=0;s<3;s++)for(int d=0;d<3;d++)for(int z=0;z<4;z++) {
        unsigned size=sizes[z],sa=bases[s]+0x1000,da=bases[d]+0x1000;
        if(s==d) {
            if(s==0){if(size>8192)continue;sa=bases[s];da=bases[d]+0x2000;}
            else da+=0x5000;
        }
        double sum=0,min=1e9,max=0;unsigned polls=0;
        for(int k=0;k<count;k++) {
            for(unsigned j=0;j<size;j++){random^=random<<13;random^=random>>17;random^=random<<5;source[j]=(unsigned char)random;poison[j]=source[j]^255;}
            if(memory(sa,source,size,1)||memory(da,poison,size,1)||memory(sa,verify,size,0)||memcmp(source,verify,size))goto done;
            if(memory(da,verify,size,0)||memcmp(poison,verify,size))goto done;
            if(!debug&&wr8(0x106,0)!=1)goto done;
            uint32_t command[]={sa,da,0x80000000u|size},ctrl;
            double start=seconds();
            if(la_blk_wr(0x1200,command,sizeof command)!=sizeof command)goto done;
            do{
                if(gap){double until=seconds()+gap/1e6;while(seconds()<until){}}
                ctrl=rd32(0x1208);polls++;
            }while(ctrl!=0xdeadbeef&&(ctrl&0x80000000u)&&seconds()-start<0.1);
            double duration=seconds()-start;
            if(ctrl==0xdeadbeef||(ctrl&0xc0000000u)){fprintf(stderr,"DDMA failure %s->%s size=%u ctrl=%08x\n",names[s],names[d],size,ctrl);goto done;}
            if(!debug&&wr8(0x106,0x69)!=1)goto done;
            if(memory(da,verify,size,0)||memcmp(source,verify,size)){fprintf(stderr,"VERIFY failure %s->%s size=%u copy=%d\n",names[s],names[d],size,k);goto done;}
            sum+=duration;if(duration<min)min=duration;if(duration>max)max=duration;
        }
        printf("%s,%s,%u,%d,%.1f,%.1f,%.1f,%.3f,%.1f\n",names[s],names[d],size,count,sum*1e6/count,min*1e6,max*1e6,size*count/sum/1e6,(double)polls/count);
    }
    result=0;
done:
    if(clock_saved&&wr32(0x80,oldclock)!=4)result=1;
    if(initialized&&(rtw_power_on()||rtw_phy_init()||rtw_set_channel(11,BW20)))result=1;
    printf("matrix %s (timing includes EP0 command/poll; excludes verification)\n",result?"FAILED":"OK");
    rtw_close();return result;
}
