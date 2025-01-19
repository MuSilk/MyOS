#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>

/**
    @brief the private reference to the super block.

    @note we need these two variables because we allow the caller to
            specify the block device and super block to use.
            Correspondingly, you should NEVER use global instance of
            them, e.g. `get_super_block`, `block_device`

    @see init_bcache
 */
static const SuperBlock *sblock;

/**
    @brief the reference to the underlying block device.
 */
static const BlockDevice *device; 

/**
    @brief global lock for block cache.

    Use it to protect anything you need.

    e.g. the list of allocated blocks, etc.
 */
static SpinLock lock;

/**
    @brief the list of all allocated in-memory block.

    We use a linked list to manage all allocated cached blocks.

    You can implement your own data structure if you like better performance.

    @see Block
 */
static ListNode head;

static LogHeader header; // in-memory copy of log header block.

/**
    @brief a struct to maintain other logging states.
    
    You may wonder where we store some states, e.g.
    
    * how many atomic operations are running?
    * are we checkpointing?
    * how to notify `end_op` that a checkpoint is done?

    Put them here!

    @see cache_begin_op, cache_end_op, cache_sync
 */
struct {
    /* your fields here */
    SpinLock lock;
    bool iscommit;
    int outstanding;
    Semaphore sem;
    Semaphore check;
} log;

// read the content from disk.
static INLINE void device_read(Block *block) {
    device->read(block->block_no+0x20800, block->data);
}

// write the content back to disk.
static INLINE void device_write(Block *block) {
    device->write(block->block_no+0x20800, block->data);
}

// read log header from disk.
static INLINE void read_header() {
    device->read(sblock->log_start+0x20800, (u8 *)&header);
}

// write log header back to disk.
static INLINE void write_header() {
    device->write(sblock->log_start+0x20800, (u8 *)&header);
}

// initialize a block struct.
static void init_block(Block *block) {
    block->block_no = 0;
    init_list_node(&block->node);
    block->acquired = false;
    block->pinned = false;

    init_sleeplock(&block->lock);
    block->valid = false;
    memset(block->data, 0, sizeof(block->data));
}

// see `cache.h`.
static usize block_num=0;
static usize get_num_cached_blocks() {
    // TODO
    return block_num;
}

// see `cache.h`.
static Block *cache_acquire(usize block_no) {
    // TODO
    acquire_spinlock(&lock);
    Block* res=NULL;
    _for_in_list(p,&head){
        if(p==&head)break;
        Block* b=container_of(p,Block,node);
        if(b->block_no==block_no){
            res=b;
            break;
        }
    }
    if(res){
        res->acquired=true;
        release_spinlock(&lock);
        if(!wait_sem(&res->lock))PANIC();
        acquire_spinlock(&lock);
        _detach_from_list(&res->node);
        _insert_into_list(&head,&res->node);
        release_spinlock(&lock);
        return res;
    }
    if(block_num>=EVICTION_THRESHOLD){
        for(ListNode* p=head.prev;p!=&head;p=p->prev){
            if(block_num<EVICTION_THRESHOLD)break;
            Block* b=container_of(p,Block,node);
            if(!b->acquired&&!b->pinned){
                ListNode* q=p;
                p=p->next;
                _detach_from_list(q);
                block_num--;
                kfree(b);
            }
        }
    }
    res=kalloc(sizeof(Block));
    init_block(res);

    res->block_no=block_no;
    res->acquired=true;
    res->valid=true;

    release_spinlock(&lock);
    if(!wait_sem(&res->lock))PANIC();
    device_read(res);
    acquire_spinlock(&lock);

    block_num++;
    _insert_into_list(&head,&res->node);
    release_spinlock(&lock);
    return res;
}

// see `cache.h`.
static void cache_release(Block *block) {
    // TODO
    acquire_spinlock(&lock);
    block->acquired=false;
    post_sem(&block->lock);
    release_spinlock(&lock);
}

SpinLock bitmap_lock;

// see `cache.h`.
void init_bcache(const SuperBlock *_sblock, const BlockDevice *_device) {
    sblock = _sblock;
    device = _device;

    // TODO
    init_spinlock(&lock);
    init_spinlock(&log.lock);
    init_spinlock(&bitmap_lock);
    init_list_node(&head);
    block_num=0;
    log.outstanding=log.iscommit=0;
    init_sem(&log.sem,0);
    init_sem(&log.check,0);
    read_header();
    for(usize i=0;i<header.num_blocks;i++){
        Block* from=cache_acquire(sblock->log_start+i+1);
        Block* to=cache_acquire(header.block_no[i]);
        memcpy(to->data,from->data,BLOCK_SIZE);
        device_write(to);
        cache_release(from);
        cache_release(to);
    }
    header.num_blocks=0;
    memset(header.block_no,0,LOG_MAX_SIZE);
    write_header();
}

// see `cache.h`.
static void cache_begin_op(OpContext *ctx) {
    // TODO
    acquire_spinlock(&log.lock);
    ctx->rm=0;
    while(log.iscommit||header.num_blocks+(log.outstanding+1)*OP_MAX_NUM_BLOCKS>LOG_MAX_SIZE){
        release_spinlock(&log.lock);
        if(!wait_sem(&log.sem))PANIC();
        acquire_spinlock(&log.lock);
    }
    log.outstanding++;
    release_spinlock(&log.lock);
}

// see `cache.h`.
static void cache_sync(OpContext *ctx, Block *block) {
    // TODO
    if(ctx==NULL){
        device_write(block);
        return ;
    }
    acquire_spinlock(&log.lock);
    block->pinned=true;
    for(usize i=0;i<header.num_blocks;i++){
        if(block->block_no==header.block_no[i]){
            release_spinlock(&log.lock);
            return;
        }
    }
    if(ctx->rm>=OP_MAX_NUM_BLOCKS||header.num_blocks>=LOG_MAX_SIZE)PANIC();
    header.block_no[header.num_blocks++]=block->block_no;
    ctx->rm++;
    release_spinlock(&log.lock);
}

// see `cache.h`.
static void cache_end_op(OpContext *ctx) {
    // TODO
    acquire_spinlock(&log.lock);
    if(log.iscommit)PANIC();
    log.outstanding--;
    if(log.outstanding>0){
        post_sem(&log.sem);
        release_spinlock(&log.lock);
        if(!wait_sem(&log.check))PANIC();
        return;
    }
    log.iscommit=1;
    for(usize i=0;i<header.num_blocks;i++){
        release_spinlock(&log.lock);

        Block* from=cache_acquire(header.block_no[i]);
        Block* to=cache_acquire(sblock->log_start+i+1);
        memcpy(to->data,from->data,BLOCK_SIZE);
        device_write(to);
        cache_release(from);
        cache_release(to);

        acquire_spinlock(&log.lock);
    }
    release_spinlock(&log.lock);
    write_header();
    acquire_spinlock(&log.lock);
    for(usize i=0;i<header.num_blocks;i++){
        Block* b=cache_acquire(header.block_no[i]);
        device_write(b);
        b->pinned=false;
        cache_release(b);
    }
    header.num_blocks=0;
    release_spinlock(&log.lock);
    write_header();
    acquire_spinlock(&log.lock);
    log.iscommit=0;
    post_all_sem(&log.sem);
    post_all_sem(&log.check);
    release_spinlock(&log.lock);
}

// see `cache.h`.
static usize cache_alloc(OpContext *ctx) {
    // TODO
    acquire_spinlock(&bitmap_lock);
    for (u32 block_head=0;block_head<sblock->num_blocks;block_head+=8*BLOCK_SIZE){
        Block* block=cache_acquire(sblock->bitmap_start+block_head/(8*BLOCK_SIZE));
        BitmapCell* bitmap=(BitmapCell*)block->data;
        for (u32 index=0;index<(8*BLOCK_SIZE)&&block_head+index<sblock->num_blocks;index++){
            if(bitmap_get(bitmap,index))continue;
            bitmap_set(bitmap,index);
            cache_sync(ctx,block);
            cache_release(block);

            Block* res=cache_acquire(block_head+index);
            memset(res->data,0,BLOCK_SIZE);
            cache_sync(ctx,res);
            cache_release(res);
            release_spinlock(&bitmap_lock);
            return block_head+index;
        }
        cache_release(block);
    }
    release_spinlock(&bitmap_lock);
    PANIC();
}

// see `cache.h`.

static void cache_free(OpContext *ctx, usize block_no) {
    // TODO
    acquire_spinlock(&bitmap_lock);
    Block* b=cache_acquire(sblock->bitmap_start+block_no/(8*BLOCK_SIZE));
    usize index=block_no%(8*BLOCK_SIZE);
    bitmap_clear((BitmapCell*)b->data,index);
    cache_sync(ctx,b);
    cache_release(b);
    release_spinlock(&bitmap_lock);
}

BlockCache bcache = {
    .get_num_cached_blocks = get_num_cached_blocks,
    .acquire = cache_acquire,
    .release = cache_release,
    .begin_op = cache_begin_op,
    .sync = cache_sync,
    .end_op = cache_end_op,
    .alloc = cache_alloc,
    .free = cache_free,
};