#include "file.h"
#include <common/defines.h>
#include <common/spinlock.h>
#include <common/sem.h>
#include <fs/inode.h>
#include <fs/pipe.h>
#include <common/list.h>
#include <kernel/mem.h>
#include <kernel/printk.h>

// the global file table.
static struct ftable ftable;

void init_ftable() {
    // TODO: initialize your ftable.
    init_spinlock(&ftable.lock);
}

void init_oftable(struct oftable *oftable) {
    // TODO: initialize your oftable for a new process.
    for(int i=0;i<NOFILE;i++)oftable->file[i]=NULL;
}

/* Allocate a file structure. */
struct file* file_alloc() {
    /* (Final) TODO BEGIN */
    acquire_spinlock(&ftable.lock);
    for(int i=0;i<NFILE;i++){
        if(ftable.files[i].ref==0){
            ftable.files[i].ref=1;
            release_spinlock(&ftable.lock);
            return &(ftable.files[i]);
        }
    }
    release_spinlock(&ftable.lock);
    /* (Final) TODO END */
    return NULL;
}

/* Increment ref count for file f. */
struct file* file_dup(struct file* f) {
    /* (Final) TODO BEGIN */
    acquire_spinlock(&ftable.lock);
    f->ref+=1;
    release_spinlock(&ftable.lock);
    /* (Final) TODO END */
    return f;
}

/* Close file f. (Decrement ref count, close when reaches 0.) */
void file_close(struct file* f) {
    /* (Final) TODO BEGIN */
    acquire_spinlock(&ftable.lock);
    f->ref--;
    if(f->ref>0){
        release_spinlock(&ftable.lock);
        return;
    }
    if(f->type==FD_PIPE){
        pipe_close(f->pipe,f->writable);
    }
    else if(f->type==FD_INODE){
        OpContext ctx;
        bcache.begin_op(&ctx);
        inodes.put(&ctx,f->ip);
        bcache.end_op(&ctx);
    }
    f->type=FD_NONE;
    release_spinlock(&ftable.lock);
    /* (Final) TODO END */
}

/* Get metadata about file f. */
int file_stat(struct file* f, struct stat* st) {
    /* (Final) TODO BEGIN */
    if(f->type==FD_INODE){
        inodes.lock(f->ip);
        stati(f->ip,st);
        inodes.unlock(f->ip);
        return 0;
    }
    /* (Final) TODO END */
    return -1;
}

/* Read from file f. */
isize file_read(struct file* f, char* addr, isize n) {
    /* (Final) TODO BEGIN */
    // printk("file_read begin\n");
    if(f->readable==0)return -1;
    if(f->type==FD_PIPE)return pipe_read(f->pipe,(u64)addr,n);
    if(f->type==FD_INODE){
        inodes.lock(f->ip);
        auto res=inodes.read(f->ip,(u8*)addr,f->off,n);
        if(res>0)f->off+=res;
        inodes.unlock(f->ip);
        return res;
    }
    /* (Final) TODO END */
    return 0;
}

/* Write to file f. */
isize file_write(struct file* f, char* addr, isize n) {
    /* (Final) TODO BEGIN */
    // printk("file_write begin\n");
    if(f->writable==0)return -1;
    if(f->type==FD_PIPE)return pipe_write(f->pipe,(u64)addr,n);
    if(f->type==FD_INODE){
        isize maxbytes=((OP_MAX_NUM_BLOCKS-4)/2)*BLOCK_SIZE;
        isize idx=0;
        while (idx<n){
            isize len=MIN(n-idx,maxbytes);
            OpContext ctx;
            bcache.begin_op(&ctx);
            inodes.lock(f->ip);
            isize reallen=inodes.write(&ctx,f->ip,(u8*)(addr+idx),f->off,len);
            if (reallen>0) f->off+=reallen;
            inodes.unlock(f->ip);
            bcache.end_op(&ctx);
            if (reallen<0) break;
            ASSERT(reallen==len);
            idx+=reallen;
        }
        if (idx==n) return n;
        return -1;
    }
    /* (Final) TODO END */
    return 0;
}