#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <common/list.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
#include <kernel/printk.h>

RefCount kalloc_page_cnt;
extern char end[];
static SpinLock memlock;

typedef struct _MyListNode{
    struct _MyListNode* next;
}MyListNode;
void add_to_list(MyListNode** head,MyListNode* p){
    p->next=*head;
    *head=p;
}
void* fetch_from_list(MyListNode** head){
    MyListNode* p=*head;
    *head=(*head)->next;
    return p;
}

static QueueNode* pages=NULL;

void kinit() {
    init_rc(&kalloc_page_cnt);
    init_spinlock(&memlock);

    for(u64 i=PAGE_BASE((u64)&end)+PAGE_SIZE*2;i+PAGE_SIZE<P2K(PHYSTOP);i+=PAGE_SIZE){
        add_to_queue(&pages,(QueueNode*)i);
        // printk("%llx\n",i);
    }
}

void* kalloc_page() {
    increment_rc(&kalloc_page_cnt);
    void *page=fetch_from_queue(&pages);
    return page;
}

void kfree_page(void* p) {
    decrement_rc(&kalloc_page_cnt);
    add_to_queue(&pages,p);
    return;
}

typedef struct _PageHead{
    u64 block_size;
}PageHead;

#define BLOCK_TYPE 25
const int BLOCK_SIZE[]={8,16,24,32,40,48,56,64,80,96,112,128,160,192,224,256,288,336,368,408,448,512,1360,2040,4096};
static MyListNode *PowerHead[BLOCK_TYPE]={NULL};
u64 PowerTail[BLOCK_TYPE]={0};

// u64 BlockPage[BLOCK_TYPE]={0};

inline int get_id(u64 size){
    for(int i=0;i<BLOCK_TYPE;i++){
        if(size<=(u64)BLOCK_SIZE[i])return i;
    }
    return BLOCK_TYPE-1;
}

void* kalloc(unsigned long long size) {
    acquire_spinlock(&memlock);
    int id=get_id(size);
    size=BLOCK_SIZE[id];

    if(!PowerHead[id]){
        if(!PowerTail[id]){
            PageHead* head=(PageHead*)kalloc_page();

            // BlockPage[id]+=1;
            // for(int i=0;i<BLOCK_TYPE;i++)printk("%lld ",BlockPage[i]);
            // printk("\n");

            head->block_size=size;
            PowerTail[id]=(u64)head+sizeof(PageHead);
        }
        add_to_list(&PowerHead[id],(MyListNode*)PowerTail[id]);
        if(PAGE_BASE((PowerTail[id]+2*size-1))!=PAGE_BASE(PowerTail[id]))PowerTail[id]=0;
        else PowerTail[id]+=size;
    }
    MyListNode *p=fetch_from_list(&PowerHead[id]);
    release_spinlock(&memlock);
    return p;
}

void kfree(void* ptr) {
    acquire_spinlock(&memlock);
    PageHead* head=(PageHead*)PAGE_BASE(ptr);
    int id=get_id(head->block_size);
    add_to_list(&PowerHead[id],(MyListNode*)ptr);
    release_spinlock(&memlock);
    return;
}
