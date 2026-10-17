#pragma once
/* RAM-only firmware support. Caller owns initialized hardware and recovery.
 * Preserve vendor CP0/stack startup, then execute our MIPS16e2 program.
 * The chipset did not execute the tested JALX/MIPS32 or PC-relative-load stubs.
 */
static int cpu_halt(void) {return put(3,rd8(3)&~4u,1)||put(0x1d,rd8(0x1d)&~1u,1);}
static int cpu_load(const uint16_t *program,unsigned bytes) {
    unsigned char code[8192]={0},verify[8192];unsigned size=0x230+bytes;
    if(size>sizeof code)return -1;
    FILE *f=fopen("../tmp/vendor-imem.bin","rb");if(!f)return -1;
    unsigned n=(unsigned)fread(code,1,0x230,f);fclose(f);if(n!=0x230)return -1;
    memcpy(code+0x230,program,bytes);size=(size+7)&~7u;
    if(cpu_halt()||put(0x1082,rd8(0x1082)&~1u,1)||put(9,rd8(9)&~0x40u,1)||
       put(0x1082,rd8(0x1082)|1,1)||put(9,rd8(9)|0x40,1)||
       put(0x1080,rd32(0x1080)|0x10100,4)||put(0x80,(rd16(0x80)&0x3800)|1,2)||
       txff_xfer(0x1000,code,size,1)||copy(0x18781000,0,size)||
       copy(0,0x18784000,size)||txff_xfer(0x4000,verify,size,0)||memcmp(code,verify,size))return -1;
    printf("Custom RAM firmware verified: %u bytes\n",size);return 0;
}
static int cpu_start(void) {
    return put(0x80,(rd16(0x80)&0x3800)|0x4078,2)||put(0x1d,rd8(0x1d)|1,1)||put(3,rd8(3)|4,1);
}
struct m16 {uint16_t code[2048];unsigned n;};
static void emit(struct m16 *p,unsigned v) {p->code[p->n++]=(uint16_t)v;}
static void ext(struct m16 *p,unsigned v){emit(p,0xf000|((v>>11)&31)|(v&0x7e0));}
static void li32(struct m16 *p,unsigned reg,uint32_t v) {
    unsigned hi=(v+0x8000u)>>16;ext(p,hi);emit(p,0x6820|(reg<<8)|(hi&31));
    ext(p,v&65535);emit(p,0x4800|(reg<<8)|(v&31));
}
static void branch(struct m16 *p,unsigned op,unsigned target){emit(p,op|((target-p->n-1)&(op==0x1000?2047:255)));}
static void addi(struct m16 *p,unsigned reg,int v){ext(p,(unsigned)v&65535);emit(p,0x4800|(reg<<8)|((unsigned)v&31));}
static void lw16(struct m16 *p,unsigned dst,unsigned base,unsigned off){emit(p,0x9800|(base<<8)|(dst<<5)|(off>>2));}
static void sw16(struct m16 *p,unsigned src,unsigned base,unsigned off){emit(p,0xd800|(base<<8)|(src<<5)|(off>>2));}
static void jump16(struct m16 *p,unsigned target){unsigned word=(0x230+target*2)>>2;emit(p,0x1800|((word>>16&31)<<5)|(word>>21&31));emit(p,word&65535);emit(p,0x6500);}
static int cpu_mem(unsigned address,unsigned char *data,unsigned bytes,int write) {
    for(unsigned off=0;off<bytes;){unsigned at=address+off,n=bytes-off;
        if(n>504)n=504;if(n>4096-(at&4095))n=4096-(at&4095);
        if(put(0x140,(at&0xffffff)>>12,2))return -1;
        int r=write?la_blk_wr(0x8000+(at&4095),data+off,n):la_blk_rd(0x8000+(at&4095),data+off,n);
        if(r!=(int)n)return -1;off+=n;
    }return 0;
}
