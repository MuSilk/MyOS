#include <aarch64/mmu.h>
#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <common/string.h>
#include <fs/block_device.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/pt.h>
#include <kernel/sched.h>


void init_sections(ListNode *section_head) {
    /* (Final) TODO BEGIN */
    init_list_node(section_head);
    /* (Final) TODO END */
}

void free_sections(struct pgdir *pd) {
    /* (Final) TODO BEGIN */
    for(auto p=pd->section_head.next;p!=&pd->section_head;){
        if(p==&pd->section_head)break;
        struct section* sec=container_of(p,struct section,stnode);
        for(u64 i=PAGE_BASE(sec->begin);i<sec->end;i+=PAGE_SIZE){
            auto pte=get_pte(pd,i,false);
            if(pte&&(*pte&PTE_VALID))kfree_page((void*)P2K(PTE_ADDRESS(*pte)));
        }
        if(sec->fp){
            file_close(sec->fp);
        }

        p=p->next;
        _detach_from_list(&sec->stnode);
        kfree(sec);
    }
    /* (Final) TODO END */
}

u64 sbrk(i64 size) {
    /**
     * (Final) TODO BEGIN 
     * 
     * Increase the heap size of current process by `size`.
     * If `size` is negative, decrease heap size. `size` must
     * be a multiple of PAGE_SIZE.
     * 
     * Return the previous heap_end.
     */

    auto pd=&thisproc()->pgdir;
    struct section* sec=NULL;
    _for_in_list(p,&pd->section_head){
        if(p==&pd->section_head)break;
        auto s=container_of(p,struct section,stnode);
        if(s->flags==ST_HEAP){
            sec=s;
            break;
        }
    };
    if(!sec)PANIC();
    u64 res=sec->end;
    sec->end+=size;
    if(size<0){
        for(u64 i=0;i<(u64)-size;i+=PAGE_SIZE){
            auto pte=get_pte(pd,sec->end+i,false);
            if(pte&&*pte){
                kfree_page((void*)P2K((*pte)&KSPACE_MASK));
                *pte=NULL;
            }
        }
    }
    arch_tlbi_vmalle1is();
    return res;
    /* (Final) TODO END */
}

int pgfault_handler(u64 iss) {
    // printk("pgfault_handler\n");
    Proc *p = thisproc();
    ASSERT(p!=NULL);
    struct pgdir *pd = &p->pgdir;
    u64 addr = arch_get_far(); // Attempting to access this address caused the page fault

    /** 
     * (Final) TODO BEGIN
     * 
     * 1. Find the section struct which contains the faulting address `addr`.
     * 2. Check section flags to determine page fault type.
     * 3. Handle the page fault accordingly.
     * 4. Return to user code or kill the process.
     */

    // printk("pagefault:%llx\n",(u64)addr);
    if(addr&KSPACE_MASK)PANIC();

    if(!_empty_list(&p->vma_head)){
        struct vma* vma=NULL;
        _for_in_list(node,&p->vma_head){
            if(node==&p->vma_head)break;
            auto v=container_of(node,struct vma,ptnode);
            if(v->start<=addr&&addr<v->end){vma=v;break;}
        }
        if(vma){
            addr=PAGE_BASE(addr);
            void* page=kalloc_page();
            vmmap(pd,addr,page,vma->permission);
            arch_tlbi_vmalle1is();
            struct file *f = vma->file;
            if(!f->readable||f->type!=FD_INODE)return -1;
            inodes.lock(f->ip);
            inodes.read(f->ip,(u8*)page,vma->off+(addr-vma->start),PAGE_SIZE);
            inodes.unlock(f->ip);
            return 0;
        }
    }

    auto pte = get_pte(pd,addr,true);
    if (*pte == NULL){
        vmmap(pd,addr,kalloc_page(),PTE_USER_DATA);
    }
    else if (PTE_FLAGS(*pte) & PTE_RO){
        auto p = kalloc_page();
        memmove(p, (void *)P2K(PTE_ADDRESS(*pte)), PAGE_SIZE);
        kfree_page((void *)P2K(PTE_ADDRESS(*pte)));
        vmmap(pd,addr,p,PTE_USER_DATA);
    }
    arch_tlbi_vmalle1is();
    // printk("pagefault:%llx dealed\n",(u64)addr);
    return 0;


    /* (Final) TODO END */
}

void copy_sections(ListNode *from_head, ListNode *to_head)
{
    /* (Final) TODO BEGIN */
    _for_in_list(p, from_head){
		if(p == from_head)break;
		struct section* sec = container_of(p, struct section, stnode);
		struct section* new_sec = kalloc(sizeof(struct section));
		memmove(new_sec, sec, sizeof(struct section));
		if(sec->fp)new_sec->fp = file_dup(sec->fp);
		_insert_into_list(to_head, &new_sec->stnode);
	}

    /* (Final) TODO END */
}
