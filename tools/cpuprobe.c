/* RAM-only MIPS CPU bring-up probe. No flash/efuse writes.
 * Loads a tiny MIPS16 JALX trampoline + MIPS32 loop into IMEM using DDMA.
 * CPU writes only a marker and counter in reserved RX SRAM, then is reset.
 * Encodings: MIPS16 JALX from GNU binutils mips16-jalx.d; standard MIPS32.
 */
#define _CRT_SECURE_NO_WARNINGS
#include "rtwinit.h"
#include "iqstream.h"
static int loadmem(const char *path,unsigned dest) {
    FILE *f=fopen(path,"rb");if(!f)return -1;
    unsigned char data[8192],verify[8192];unsigned total=0;int result=-1;
    for(;;){unsigned n=(unsigned)fread(data,1,sizeof data,f);if(!n)break;
        if(txff_xfer(0x1000,data,n,1)||copy(0x18781000,dest+total,n)||
           copy(dest+total,0x18784000,n)||txff_xfer(0x4000,verify,n,0)||memcmp(data,verify,n))goto done;
        total+=n;
    }
    if(!ferror(f)){printf("Loaded and verified %s bytes=%u at %08x\n",path,total,dest);result=0;}
done:fclose(f);return result;
}
int main(int argc,char **argv) {
    unsigned char code[1024]={0},check[1024],zero[16]={0};int result=1;
    const uint16_t trampoline[]={0x1c00,0x0080,0x6500,0x6500};
    const uint32_t program[]={
        0x3c08b870, /* lui t0,0xb870: uncached RX SRAM */
        0x35081000, /* ori t0,t0,0x1000 */
        0x3c094350, /* lui t1,0x4350 */
        0x3529554f, /* ori t1,t1,0x554f */
        0xad090000, /* sw t1,0(t0) */
        0x240a0000, /* addiu t2,zero,0 */
        0x254a0001, /* loop: addiu t2,t2,1 */
        0xad0a0004, /* sw t2,4(t0) */
        0x1000fffd, /* beq zero,zero,loop */
        0x00000000  /* nop (delay slot) */
    };
    memcpy(code,trampoline,sizeof trampoline);memcpy(code+0x200,program,sizeof program);
    int isa32=argc>1?atoi(argv[1]):0;
    if(isa32==1){uint32_t entry[2]={0x08000080,0};memcpy(code,entry,sizeof entry);}
    int dmem=argc>2?atoi(argv[2]):0;
    int vendor=argc>3?atoi(argv[3]):0;
    if(dmem){uint32_t instruction=0x3c088020;memcpy(code+0x200,&instruction,4);}
    if(isa32==2){
        const uint16_t simple[]={0xb204,0x6b55,0xda60,0x4b01,0xda61,0x17fd,0x6500,0x6500};
        uint32_t address=dmem?0x80201000:0xb8701000;
        memcpy(code,simple,sizeof simple);memcpy(code+16,&address,4);
    }
    if(isa32==3){
        uint16_t simple[]={dmem?0xf030:0xf077,dmem?0x6a20:0x6a30,
            0xf002,0x4a00,0x6b55,0xda60,0x4b01,0xda61,0x17fd,0x6500};
        memcpy(code,simple,sizeof simple);
    }
    setvbuf(stdout,NULL,_IONBF,0);if(rtw_open())return 1;
    if(rtw_power_on()||rtw_read_efuse()||rtw_phy_init()||rtw_set_channel(11,BW20))goto done;
    if(put(0x100,rd16(0x100)&~0xc0u,2)||put(0x6a0,0,2)||put(0x6a2,0,2)||put(0x6a4,0,2))goto done;
    if(put(0x03,rd8(0x03)&~4u,1)||put(0x1d,rd8(0x1d)&~1u,1))goto done;
    /* HALMAC platform reset includes the documented 8822B clock sync fix. */
    if(put(0x1082,rd8(0x1082)&~1u,1)||put(9,rd8(9)&~0x40u,1)||
       put(0x1082,rd8(0x1082)|1,1)||put(9,rd8(9)|0x40,1))goto done;
    if(put(0x1080,rd32(0x1080)|0x10100,4)||put(0x80,(rd16(0x80)&0x3800)|1,2)||
       txff_xfer(0x1000,code,sizeof code,1)||copy(0x18781000,0,sizeof code)||
       copy(0,0x18782000,sizeof code)||txff_xfer(0x2000,check,sizeof check,0))goto done;
    if(memcmp(code,check,sizeof code)){fprintf(stderr,"IMEM roundtrip mismatch\n");goto done;}
    puts("IMEM byte-for-byte roundtrip verified");
    if(vendor&&(loadmem("../tmp/vendor-dmem.bin",0x200000)||loadmem("../tmp/vendor-imem.bin",0)))goto done;
    if(vendor==2){
        if(isa32>=2){if(txff_xfer(0x1000,code,20,1)||copy(0x18781000,0x230,20))goto done;}
        else {
            uint16_t jump[]={0x1c00,0x0100,0x6500,0x6500};
            if(txff_xfer(0x1000,(unsigned char*)jump,sizeof jump,1)||copy(0x18781000,0x230,sizeof jump)||
               txff_xfer(0x1000,code+0x200,sizeof program,1)||copy(0x18781000,0x400,sizeof program))goto done;
        }
        puts("Probe inserted after vendor CP0 initialization at 0x230");
    }
    if(put(0x140,0x701,2)||la_blk_wr(0x8000,zero,sizeof zero)!=sizeof zero)goto done;
    if(dmem&&copy(0x18701000,0x201000,16))goto done;
    if(put(0x80,(rd16(0x80)&0x3800)|0x4078,2)||
       put(0x1d,rd8(0x1d)|1,1)||put(0x03,rd8(0x03)|4,1))goto done;
    for(int i=0;i<(dmem?1:10);i++) {
        Sleep(10);uint32_t words[4];if(la_blk_rd(0x8000,words,sizeof words)!=sizeof words)goto done;
        printf("CPU marker=%08x counter=%u FW=%08x DBG7=%08x\n",words[0],words[1],rd32(0x80),rd32(0x10fc));
        if(words[0]==(isa32>=2?0x55u:0x4350554fu)&&words[1])result=0;
        if(vendor==1&&rd16(0x80)==0xc078)result=0;
    }
    if(dmem){
        if(put(0x03,rd8(0x03)&~4u,1)||copy(0x201000,0x18701000,16))goto done;
        uint32_t words[4];if(la_blk_rd(0x8000,words,sizeof words)!=sizeof words)goto done;
        printf("DMEM marker=%08x counter=%u\n",words[0],words[1]);
        if(words[0]==(isa32>=2?0x55u:0x4350554fu)&&words[1])result=0;
    }
done:
    /* Always halt RAM code before full PHY recovery. */
    if(put(0x03,rd8(0x03)&~4u,1)||put(0x80,0,2)||rtw_power_on()||rtw_phy_init()||rtw_set_channel(11,BW20))result=1;
    printf("CPU probe %s\n",result?"FAILED":"OK");rtw_close();return result;
}
