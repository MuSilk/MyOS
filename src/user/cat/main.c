#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    /* (Final) TODO BEGIN */
    for(int i=1;i<argc;i++){
        int fd=open(argv[i],O_RDONLY);
        if(fd<0)continue;

        int n;char buf[512];
        while((n=read(fd,buf,512))>0){
            for(int j=0;j<n;j++){
                fprintf(stdout,"%c",buf[j]);
            }
        }
        close(fd);
    }
    if(argc==1){
        int n;char buf[512];
        while((n=read(0,buf,512))>0){
            for(int j=0;j<n;j++){
                fprintf(stdout,"%c",buf[j]);
            }
        }
    }
    /* (Final) TODO END */
    return 0;
}