#pragma once
/* Overlap alone accepts a frozen ring. Compare IQ at each address with its
 * previous revolution, ignoring metadata and host-injected descriptor bits.
 * A nearly constant input is deliberately rejected as unverified freshness. */
struct iq_freshness { uint32_t previous[LA_NSMP]; unsigned checks; };
static int iq_check_freshness(struct iq_freshness *state,const unsigned char *raw,
                              unsigned count,unsigned long long absolute) {
    unsigned compared=0,repeated=0;
    for(unsigned j=0;j<count;j++) {
        uint32_t value;memcpy(&value,raw+j*8,4);value&=0xffffff;
        unsigned index=(unsigned)((absolute+j)%LA_NSMP);
        if(absolute+j>=LA_NSMP){compared++;repeated+=state->previous[index]==value;}
        state->previous[index]=value;
    }
    if(compared>count/2) {
        state->checks++;
        if(repeated*100u>=compared*95u){
            fprintf(stderr,"FROZEN/REPEATING IQ sample=%llu identical=%u/%u at one-ring lag\n",absolute,repeated,compared);return -1;
        }
    }
    return 0;
}
