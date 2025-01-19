#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    /* (Final) TODO BEGIN */
    if(argc<2){
        printf("parameter error\n");
        exit(-1);
    }
    for(int i=1;i<argc;i++){
        if(mkdir(argv[i],O_RDONLY)<0){
            printf("%s create failed\n",argv[i]);
        }
    }
    /* (Final) TODO END */
    exit(0);
}