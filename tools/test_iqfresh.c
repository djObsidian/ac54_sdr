/* Hardware-free regression: reject repeated SRAM even if a few words change. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define LA_NSMP 16384u
#include "iqfresh.h"
static int run(int dynamic,int perturb) {
    struct iq_freshness state={0};unsigned char raw[1984*8];
    for(unsigned block=0;block<32;block++) {
        for(unsigned j=0;j<1984;j++) {
            unsigned absolute=block*1984+j,index=absolute%LA_NSMP;
            uint64_t value=((dynamic?absolute:index)*7919u)&0xffffff;
            /* Mimic sparse descriptor edits in an otherwise frozen ring. */
            if(perturb&&index%512<3)value=(value+block)&0xffffff;
            value|=(uint64_t)block<<32;memcpy(raw+j*8,&value,8);
        }
        if(iq_check_freshness(&state,raw,1984,block*1984))return 1;
    }
    return 0;
}
int main(void) {
    if(!run(0,0)||!run(0,1)||run(1,0)){puts("freshness regression FAILED");return 1;}
    puts("freshness regression OK: static and sparsely edited rings rejected; advancing IQ accepted");return 0;
}
