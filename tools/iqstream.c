/* Continuous IQ to signed little-endian int16 IQ file. Full initialization each run. */
#define _CRT_SECURE_NO_WARNINGS
#include "rtwinit.h"
#include "iqstream.h"
#include <io.h>
#include <fcntl.h>
static int binary_sink(const int16_t *iq,int n,void *user)
{
    return fwrite(iq,4,n,(FILE*)user)==(size_t)n?0:-1;
}
int main(int argc,char **argv)
{
    int seconds=2,rate=6,ch=11;
    const char *path="stream.cs16";
    for(int i=1;i<argc;i++) {
        if(sscanf(argv[i],"seconds=%d",&seconds)==1)continue;
        if(sscanf(argv[i],"rate=%d",&rate)==1)continue;
        if(sscanf(argv[i],"ch=%d",&ch)==1)continue;
        if(!strncmp(argv[i],"out=",4)){path=argv[i]+4;continue;}
        fprintf(stderr,"usage: iqstream [seconds=2] [rate=3..6] [ch=11] [out=stream.cs16|-]\n");return 1;
    }
    if(seconds<1||seconds>3600||rate<3||rate>6||ch<1||ch>177)return 1;
    setvbuf(stdout,NULL,_IONBF,0);
    FILE *binary=NULL;
    if(!strcmp(path,"-")) {
        int fd=_dup(_fileno(stdout));if(fd<0)return 1;
        if(_setmode(fd,_O_BINARY)<0){_close(fd);return 1;}
        binary=_fdopen(fd,"wb");if(!binary){_close(fd);return 1;}
        /* Preserve the binary destination while sending all register/tool
         * diagnostics (including existing init printf calls) to stderr. */
        if(_dup2(_fileno(stderr),_fileno(stdout))){fclose(binary);return 1;}
    }
    if(rtw_open()){if(binary)fclose(binary);return 1;}
    int result=1;
    if(rd8(0xff)!=0x20){fprintf(stderr,"Run usb3 first\n");goto done;}
    if(rtw_power_on()||rtw_read_efuse()||rtw_phy_init()||rtw_set_channel((uint8_t)ch,BW20))goto done;
    SetConsoleCtrlHandler(on_stop,TRUE);
    result=iq_stream_run(seconds,rate,binary?NULL:path,binary?binary_sink:NULL,binary);
    SetConsoleCtrlHandler(on_stop,FALSE);
done:
    if(binary && fclose(binary))result=1;
    rtw_close();return result;
}
