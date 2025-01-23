#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fs/defines.h>
#include <sys/wait.h>

void fork_test(){
    printf("fork test starting\n");
    int n=100;
    int cnt[100]={0};

    for(int i=0;i<n;i++){
        int res=fork();
        if(res==0){
            cnt[i]++;
            printf("fork:%d start\n",i);
            exit(0);
        }
    }

    for(int i=0;i<n;i++)wait(NULL);

    printf("fork test success\n");
}



int main(int argc, char *argv[])
{
    fork_test();
    exit(0);
}