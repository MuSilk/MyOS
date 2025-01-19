#include <kernel/mem.h>
#include <kernel/sched.h>
#include <fs/pipe.h>
#include <common/string.h>
#include <kernel/printk.h>

void init_pipe(Pipe *pi)
{
    /* (Final) TODO BEGIN */
    init_spinlock(&pi->lock);
    init_sem(&pi->rlock,0);
    init_sem(&pi->wlock,0);
    pi->readopen=1;
    pi->writeopen=1;
    pi->nread=pi->nwrite=0;
    /* (Final) TODO END */
}

void init_read_pipe(File *readp, Pipe *pipe)
{
    /* (Final) TODO BEGIN */
    readp->type=FD_PIPE;
    readp->pipe=pipe;
    readp->readable=1;
    readp->writable=0;
    /* (Final) TODO END */
}

void init_write_pipe(File *writep, Pipe *pipe)
{
    /* (Final) TODO BEGIN */
    writep->type=FD_PIPE;
    writep->pipe=pipe;
    writep->readable=0;
    writep->writable=1;
    /* (Final) TODO END */
}

int pipe_alloc(File **f0, File **f1)
{
    /* (Final) TODO BEGIN */
    *f0=file_alloc();
    *f1=file_alloc();
    if(!*f0||!*f1){
        if(*f0)file_close(*f0);
        if(*f1)file_close(*f1);
        return -1;
    }
    Pipe* pipe=kalloc(sizeof(Pipe));
    init_pipe(pipe);
    init_read_pipe(*f0,pipe);
    init_write_pipe(*f1,pipe);
    return 0;
    /* (Final) TODO END */
}

void pipe_close(Pipe *pi, int writable)
{
    /* (Final) TODO BEGIN */
    acquire_spinlock(&pi->lock);
    if(writable){
        pi->writeopen=0;
        post_all_sem(&pi->rlock);
    }
    else{
        pi->readopen=0;
        post_all_sem(&pi->wlock);
    }
    if(pi->writeopen==0&&pi->readopen==0){
        kfree(pi);
        return;
    }
    release_spinlock(&pi->lock);
    /* (Final) TODO END */
}

int pipe_write(Pipe *pi, u64 addr, int n)
{
    /* (Final) TODO BEGIN */
    // printk("pipe write begin\n");
    acquire_spinlock(&pi->lock);
    int nwrite=0;
    for(int i=0;i<n;i++){
        while(PIPE_SIZE==pi->nwrite-pi->nread){
            if(pi->readopen){
                post_all_sem(&pi->rlock);
                release_spinlock(&pi->lock);
                if(!wait_sem(&pi->wlock))PANIC();
                acquire_spinlock(&pi->lock);
            }
            else break;
        }
        if(pi->nwrite-pi->nread<PIPE_SIZE){
            pi->data[pi->nwrite%PIPE_SIZE]=((char*)addr)[i];
            pi->nwrite++;
            nwrite++;
        }
    }
    release_spinlock(&pi->lock);
    post_all_sem(&pi->rlock);
    // printk("pipe write end\n");
    return nwrite;
    /* (Final) TODO END */
}

int pipe_read(Pipe *pi, u64 addr, int n)
{
    /* (Final) TODO BEGIN */
    // printk("pipe read begin\n");
    int nread=0;
    acquire_spinlock(&pi->lock);
    for(int i=0;i<n;i++){
        while(pi->nwrite==pi->nread){
            if(pi->writeopen==1){
                post_all_sem(&pi->wlock);
                release_spinlock(&pi->lock);
                if(!wait_sem(&pi->rlock))PANIC();
                acquire_spinlock(&pi->lock);
            }
            else break;
        }
        if(pi->nwrite>pi->nread){
            ((char*)addr)[i]=pi->data[pi->nread%PIPE_SIZE];
            pi->nread++;
            nread++;
        }
    }
    release_spinlock(&pi->lock);
    post_all_sem(&pi->wlock);
    // printk("pipe read end\n");
    return nread;
    /* (Final) TODO END */
}