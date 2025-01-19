#include <aarch64/intrinsic.h>
#include <common/string.h>
#include <kernel/mem.h>
#include <kernel/pt.h>

#include <kernel/printk.h>

extern struct page refpage[PAGE_TOTAL];

static void* fetch_page(){
    void* p=kalloc_page();
    memset(p,0,PAGE_SIZE);
    return p;
}

PTEntriesPtr get_pte(struct pgdir *pgdir, u64 va, bool alloc)
{
    // TODO:
    // Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
    // If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or return NULL if false.
    // THIS ROUTINUE GETS THE PTE, NOT THE PAGE DESCRIBED BY PTE.

    PTEntriesPtr p0=pgdir->pt,p1,p2,p3;
    if(p0==NULL){
        if(!alloc)return NULL;
        pgdir->pt=p0=fetch_page();
    }
    if (!(p0[VA_PART0(va)]&PTE_VALID)){
        if (!alloc) return NULL;
        p0[VA_PART0(va)]=K2P(fetch_page())|PTE_TABLE;
    }
    p1=(PTEntriesPtr)P2K(PTE_ADDRESS(p0[VA_PART0(va)]));
    if (!(p1[VA_PART1(va)]&PTE_VALID)){
        if (!alloc) return NULL;
        p1[VA_PART1(va)]=K2P(fetch_page())|PTE_TABLE;
    }
    p2=(PTEntriesPtr)P2K(PTE_ADDRESS(p1[VA_PART1(va)]));
    if (!(p2[VA_PART2(va)]&PTE_VALID)){
        if (!alloc) return NULL;
        p2[VA_PART2(va)]=K2P(fetch_page())|PTE_TABLE;
    }
    p3=(PTEntriesPtr)P2K(PTE_ADDRESS(p2[VA_PART2(va)]));
    return &p3[VA_PART3(va)];
}

void init_pgdir(struct pgdir *pgdir)
{
    pgdir->pt = NULL;
    init_spinlock(&pgdir->lock);
    init_list_node(&pgdir->section_head);
}

static void free_page(PTEntriesPtr p,int dep){
    if(dep==3||p==NULL){
        kfree_page(p);
        return;
    }
    for(int i=0;i<N_PTE_PER_TABLE;i++){
        if(p[i]!=NULL){
            free_page((PTEntriesPtr)P2K(PTE_ADDRESS(p[i])),dep+1);
            p[i]=NULL;
        }
    }
    kfree_page(p);
}

void free_pgdir(struct pgdir *pgdir)
{
    // TODO:
    // Free pages used by the page table. If pgdir->pt=NULL, do nothing.
    // DONT FREE PAGES DESCRIBED BY THE PAGE TABLE

    if(pgdir->pt==NULL)return;
    free_page(pgdir->pt,0);
    pgdir->pt=NULL;
}

void attach_pgdir(struct pgdir *pgdir)
{
    extern PTEntries invalid_pt;
    if (pgdir->pt)
        arch_set_ttbr0(K2P(pgdir->pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}

/**
 * Map virtual address 'va' to the physical address represented by kernel
 * address 'ka' in page directory 'pd', 'flags' is the flags for the page
 * table entry.
 */
void vmmap(struct pgdir *pd, u64 va, void *ka, u64 flags)
{
    /* (Final) TODO BEGIN */
    auto pte=get_pte(pd,va,true);
    *pte=K2P(ka)|flags;
    increment_rc(&refpage[K2P(ka)/PAGE_SIZE].ref);
    /* (Final) TODO END */
}

void vmunmap(struct pgdir *pd, u64 va){
    auto pte=get_pte(pd,va,false);
    if(pte==NULL)return;
    decrement_rc(&refpage[PTE_ADDRESS(*pte)/PAGE_SIZE].ref);
    *pte=NULL;
}

/*
 * Copy len bytes from p to user address va in page table pgdir.
 * Allocate physical pages if required.
 * Useful when pgdir is not the current page table.
 */
int copyout(struct pgdir *pd, void *va, void *p, usize len)
{
    /* (Final) TODO BEGIN */
    while(len>0){
        usize pgoff=(usize)va%PAGE_SIZE;
        u64* pte=get_pte(pd,(u64)va,1);
        if(pte==NULL)return -1;

        void* page;
        if(*pte&PTE_VALID)page=(void*)P2K(PTE_ADDRESS(*pte));
        else{
            page=kalloc_page();
            *pte=K2P(page)|PTE_USER_DATA;
        }

        usize l=MIN(PAGE_SIZE-pgoff,len);
        if(p){
            memcpy(page+pgoff,p,l);
            p+=l;
        }
        else{
            memset(page+pgoff,0,l);
        }
        va+=l;
        len-=l;
    }
    return 0;
    /* (Final) TODO END */
}