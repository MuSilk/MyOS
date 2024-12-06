#include <kernel/pt.h>
#include <kernel/mem.h>
#include <common/string.h>
#include <aarch64/intrinsic.h>

#include <kernel/printk.h>

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
