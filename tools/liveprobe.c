/* Inspect running LA ring and report register. Requires init.exe first. */
#include "la.h"
#include <stdlib.h>
int main(int argc,char **argv)
{
    int rate=argc>1?atoi(argv[1]):7;
    if(rate<0||rate>7)return 1;
    if(rtw_open())return 1;
    uint32_t old[]={rd32(0x8f8),rd32(0x8b4),rd32(0x95c),rd32(0x8fc)};
    wmask(0x8f8,0x3c00000,0);wmask(0x8b4,0x80,1);
    wmask(0x95c,0xf00,0);wmask(0x95c,0xe0,rate);wmask(0x95c,0x1f,3);
    wr32(0x8fc,0);wr8(0x7c0,0);wr8(0x7c0,0xcb);
    printf("rate=%d reg95c=%08x\n",rate,rd32(0x95c));
    LARGE_INTEGER freq,start,now,last;QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&start);last=start;
    uint32_t previous=0;int changes=0;
    for(int i=0;i<10000 && changes<20;i++) {
        uint32_t sample[2];
        if(txff_xfer(LA_START,(uint8_t*)sample,8,0))break;
        QueryPerformanceCounter(&now);
        if(sample[0]!=previous) {
            printf("%.3f ms delta=%.3f first=%08x %08x\n",1000.0*(now.QuadPart-start.QuadPart)/freq.QuadPart,1000.0*(now.QuadPart-last.QuadPart)/freq.QuadPart,sample[0],sample[1]);
            last=now;previous=sample[0];changes++;
        }
    }
    wr8(0x7c0,0xeb);Sleep(20);
    printf("triggered LA=%08x\n",rd32(0x7c0));
    wr8(0x7c0,0);
    wr32(0x8f8,old[0]);wr32(0x8b4,old[1]);wr32(0x95c,old[2]);wr32(0x8fc,old[3]);
    rtw_close();return 0;
}
