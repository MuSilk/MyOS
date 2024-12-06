#include <common/string.h>
#include <fs/inode.h>
#include <kernel/mem.h>
#include <kernel/printk.h>

/**
    @brief the private reference to the super block.

    @note we need these two variables because we allow the caller to
            specify the block cache and super block to use.
            Correspondingly, you should NEVER use global instance of
            them.

    @see init_inodes
 */
static const SuperBlock* sblock;

/**
    @brief the reference to the underlying block cache.
 */
static const BlockCache* cache;

/**
    @brief global lock for inode layer.

    Use it to protect anything you need.

    e.g. the list of allocated blocks, ref counts, etc.
 */
static SpinLock lock;

/**
    @brief the list of all allocated in-memory inodes.

    We use a linked list to manage all allocated inodes.

    You can implement your own data structure if you want better performance.

    @see Inode
 */
static ListNode head;


// return which block `inode_no` lives on.
static INLINE usize to_block_no(usize inode_no) {
    return sblock->inode_start + (inode_no / (INODE_PER_BLOCK));
}

// return the pointer to on-disk inode.
static INLINE InodeEntry* get_entry(Block* block, usize inode_no) {
    return ((InodeEntry*)block->data) + (inode_no % INODE_PER_BLOCK);
}

// return address array in indirect block.
static INLINE u32* get_addrs(Block* block) {
    return ((IndirectBlock*)block->data)->addrs;
}

// initialize inode tree.
void init_inodes(const SuperBlock* _sblock, const BlockCache* _cache) {
    init_spinlock(&lock);
    init_list_node(&head);
    sblock = _sblock;
    cache = _cache;

    if (ROOT_INODE_NO < sblock->num_inodes)
        inodes.root = inodes.get(ROOT_INODE_NO);
    else
        printk("(warn) init_inodes: no root inode.\n");
}

// initialize in-memory inode.
static void init_inode(Inode* inode) {
    init_sleeplock(&inode->lock);
    init_rc(&inode->rc);
    init_list_node(&inode->node);
    inode->inode_no = 0;
    inode->valid = false;
}

// see `inode.h`.
static usize inode_alloc(OpContext* ctx, InodeType type) {
    ASSERT(type != INODE_INVALID);

    // TODO
    for(usize i=1;i<sblock->num_inodes;i++){
        usize block_no=to_block_no(i);
        Block* block=cache->acquire(block_no);
        InodeEntry* entry=get_entry(block,i);

        if(entry->type==INODE_INVALID){
            memset(entry,0,sizeof(InodeEntry));
            entry->type=type;
            cache->sync(ctx,block);
            cache->release(block);
            return i;
        }
        cache->release(block);
    }
    return 0;
}

// see `inode.h`.
static void inode_lock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
    unalertable_wait_sem(&inode->lock);
}

// see `inode.h`.
static void inode_unlock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
    post_sem(&inode->lock);
}

// see `inode.h`.
static void inode_sync(OpContext* ctx, Inode* inode, bool do_write) {
    // TODO
    usize block_no=to_block_no(inode->inode_no);
    Block* block=cache->acquire(block_no);
    if(inode->valid&&do_write){
        memcpy(get_entry(block,inode->inode_no),&inode->entry,sizeof(InodeEntry));
        cache->sync(ctx,block); 
    }
    else if(!inode->valid){
        memcpy(&inode->entry,get_entry(block,inode->inode_no),sizeof(InodeEntry));
        inode->valid=true;
    }
    cache->release(block);
}

// see `inode.h`.
static Inode* inode_get(usize inode_no) {
    ASSERT(inode_no > 0);
    ASSERT(inode_no < sblock->num_inodes);
    acquire_spinlock(&lock);
    // TODO
    _for_in_list(p,&head){
        if(p==&head)break;
        auto inode=container_of(p,Inode,node);
        if(inode->inode_no==inode_no){
            increment_rc(&inode->rc);
            release_spinlock(&lock);
            return inode;
        }
    }

    Inode* inode=kalloc(sizeof(Inode));
    init_inode(inode);
    inode->inode_no=inode_no;
    increment_rc(&inode->rc);
    inode_lock(inode);
    inode_sync(NULL,inode,false);
    inode_unlock(inode);
    _insert_into_list(&head,&inode->node);
    release_spinlock(&lock);
    return inode;
}
// see `inode.h`.
static void inode_clear(OpContext* ctx, Inode* inode) {
    // TODO
    if(inode->entry.indirect!=0){
        Block* block=cache->acquire(inode->entry.indirect);
        auto addrs=get_addrs(block);
        for(usize i=0;i<INODE_NUM_INDIRECT;i++){
            if(addrs[i])cache->free(ctx,addrs[i]);
        }
        cache->release(block);
        cache->free(ctx,inode->entry.indirect);
        inode->entry.indirect=0;
    }
    for(usize i=0;i<INODE_NUM_DIRECT;i++){
        if(inode->entry.addrs[i]){
            cache->free(ctx,inode->entry.addrs[i]);
            inode->entry.addrs[i]=0;
        }
    }
    inode->entry.num_bytes=0;
    inode_sync(ctx,inode,true);
}

// see `inode.h`.
static Inode* inode_share(Inode* inode) {
    // TODO
    increment_rc(&inode->rc);
    return inode;
}

// see `inode.h`.
static void inode_put(OpContext* ctx, Inode* inode) {
    // TODO
    inode_lock(inode);
    decrement_rc(&inode->rc);
    if(inode->rc.count==0&&inode->entry.num_links==0){
        inode->entry.type=INODE_INVALID;
        inode_clear(ctx,inode);
        inode_sync(ctx,inode,true);
        acquire_spinlock(&lock);
        _detach_from_list(&inode->node);
        release_spinlock(&lock);
        post_sem(&inode->lock);
        kfree(inode);
        return;
    }
    post_sem(&inode->lock);
}

/**
    @brief get which block is the offset of the inode in.

    e.g. `inode_map(ctx, my_inode, 1234, &modified)` will return the block_no
    of the block that contains the 1234th byte of the file
    represented by `my_inode`.

    If a block has not been allocated for that byte, `inode_map` will
    allocate a new block and update `my_inode`, at which time, `modified`
    will be set to true.

    HOWEVER, if `ctx == NULL`, `inode_map` will NOT try to allocate any new block,
    and when it finds that the block has not been allocated, it will return 0.
    
    @param[out] modified true if some new block is allocated and `inode`
    has been changed.

    @return usize the block number of that block, or 0 if `ctx == NULL` and
    the required block has not been allocated.

    @note the caller must hold the lock of `inode`.
 */
static usize inode_map(OpContext* ctx,
                       Inode* inode,
                       usize offset,
                       bool* modified) {
    // TODO
    if(offset<INODE_NUM_DIRECT){
        if(inode->entry.addrs[offset]==0){
            if(modified)*modified=1;
            inode->entry.addrs[offset]=cache->alloc(ctx);
            inode_sync(ctx,inode,true);
        }
        return inode->entry.addrs[offset];
    }
    offset-=INODE_NUM_DIRECT;
    if(inode->entry.indirect==0){
        inode->entry.indirect=cache->alloc(ctx);
        inode_sync(ctx,inode,1);
    }
    Block* block=cache->acquire(inode->entry.indirect);
    auto addrs=get_addrs(block);
    if(addrs[offset]==0){
        if(modified)*modified=1;
        addrs[offset]=cache->alloc(ctx);
        cache->sync(ctx,block);
    }
    usize res=addrs[offset];
    cache->release(block);
    return res;
}

// see `inode.h`.
static usize inode_read(Inode* inode, u8* dest, usize offset, usize count) {
    InodeEntry* entry = &inode->entry;
    if (count + offset > entry->num_bytes)
        count = entry->num_bytes - offset;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= entry->num_bytes);
    ASSERT(offset <= end);

    // TODO
    for(usize i=offset/BLOCK_SIZE;i*BLOCK_SIZE<end;i++){
        usize l=i*BLOCK_SIZE;if(l<offset)l=offset;
        usize r=(i+1)*BLOCK_SIZE;if(r>end)r=end;
        usize len=r-l;

        usize block_no=inode_map(NULL,inode,i,NULL);
        Block* block=cache->acquire(block_no);
        memcpy(dest,block->data+(l==offset?offset%BLOCK_SIZE:0),len);
        dest+=len;
        cache->release(block);
    }
    return count;
}

// see `inode.h`.
static usize inode_write(OpContext* ctx,
                         Inode* inode,
                         u8* src,
                         usize offset,
                         usize count) {
    InodeEntry* entry = &inode->entry;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= INODE_MAX_BYTES);
    ASSERT(offset <= end);

    // TODO
    if(entry->num_bytes<end){
        entry->num_bytes=end;
        inode_sync(ctx,inode,true);
    }
    for(usize i=offset/BLOCK_SIZE;i*BLOCK_SIZE<end;i++){
        usize l=i*BLOCK_SIZE;if(l<offset)l=offset;
        usize r=(i+1)*BLOCK_SIZE;if(r>end)r=end;
        usize len=r-l;

        usize block_no=inode_map(ctx,inode,i,NULL);
        Block* block=cache->acquire(block_no);
        memcpy(block->data+(l==offset?offset%BLOCK_SIZE:0),src,len);
        src+=len;
        cache->sync(ctx,block);
        cache->release(block);
    }
    return count;
}

// see `inode.h`.
static usize inode_lookup(Inode* inode, const char* name, usize* index) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    auto name_len=strlen(name);
    for(usize i=0;i<entry->num_bytes;i+=sizeof(DirEntry)){
        DirEntry dir;
        inode_read(inode,(u8*)&dir,i,sizeof(DirEntry));
        if(dir.inode_no&&name_len==strlen(dir.name)&&strncmp(name,dir.name,name_len)==0){
            if(index)*index=i;
            return dir.inode_no;
        }
    }
    return 0;
}

// see `inode.h`.
static usize inode_insert(OpContext* ctx,
                          Inode* inode,
                          const char* name,
                          usize inode_no) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    usize index=INODE_MAX_BYTES;
    auto name_len=strlen(name);
    for(usize i=0;i<entry->num_bytes;i+=sizeof(DirEntry)){
        DirEntry dir;
        inode_read(inode,(u8*)&dir,i,sizeof(DirEntry));
        if(dir.inode_no==0&&index==INODE_MAX_BYTES)index=i;
        if(dir.inode_no&&name_len==strlen(dir.name)&&strncmp(name,dir.name,name_len)==0)return -1;
    }
    if(index==INODE_MAX_BYTES)index=entry->num_bytes;
    DirEntry dir;
    strncpy(dir.name,name,FILE_NAME_MAX_LENGTH);
    dir.inode_no=inode_no;
    inode_write(ctx,inode,(u8*)&dir,index,sizeof(DirEntry));
    return index;
}

// see `inode.h`.
static void inode_remove(OpContext* ctx, Inode* inode, usize index) {
    // TODO
    DirEntry dir;
    inode_read(inode,(u8*)&dir,index,sizeof(DirEntry));
    dir.inode_no=0;
    inode_write(ctx,inode,(u8*)&dir,index,sizeof(DirEntry));
}

InodeTree inodes = {
    .alloc = inode_alloc,
    .lock = inode_lock,
    .unlock = inode_unlock,
    .sync = inode_sync,
    .get = inode_get,
    .clear = inode_clear,
    .share = inode_share,
    .put = inode_put,
    .read = inode_read,
    .write = inode_write,
    .lookup = inode_lookup,
    .insert = inode_insert,
    .remove = inode_remove,
};