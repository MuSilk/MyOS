#include <elf.h>
#include <common/string.h>
#include <common/defines.h>
#include <kernel/console.h>
#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/pt.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <aarch64/trap.h>
#include <fs/file.h>
#include <fs/inode.h>

#define STACK_PAGE_SIZE 10
#define USERTOP         0x0001000000000000

extern int fdalloc(struct file *f);

void execve_error(struct pgdir* pd,Inode* ip,OpContext* ctx){
    free_pgdir(pd);
    kfree(pd);
    inodes.unlock(ip);
    inodes.put(ctx,ip);
    bcache.end_op(ctx);

}

int execve(const char *path, char *const argv[], char *const envp[])
{
    /* (Final) TODO BEGIN */
    // printk("execve path:%s argv:",path);
    // if(argv)for(int i=0;argv[i];++i)printk("%s\t",argv[i]);
    // printk("\n");

    struct pgdir* pd=kalloc(sizeof(struct pgdir));
    init_pgdir(pd);
    if(pd==NULL)return -1;

    OpContext ctx;
    bcache.begin_op(&ctx);
    Inode* ip=namei(path,&ctx);
    
    if(ip==NULL){
        bcache.end_op(&ctx);
        free_pgdir(pd);
        return -1;
    }

  
    inodes.lock(ip);
    Elf64_Ehdr elf;
    
    if(inodes.read(ip,(u8*)&elf,0,sizeof(Elf64_Ehdr))!=sizeof(Elf64_Ehdr)){
        execve_error(pd,ip,&ctx);
        return -1;
    }
   
    if(elf.e_ident[EI_MAG0]!=ELFMAG0){execve_error(pd,ip,&ctx);return -1;}
    if(elf.e_ident[EI_MAG1]!=ELFMAG1){execve_error(pd,ip,&ctx);return -1;}
    if(elf.e_ident[EI_MAG2]!=ELFMAG2){execve_error(pd,ip,&ctx);return -1;}
    if(elf.e_ident[EI_MAG3]!=ELFMAG3){execve_error(pd,ip,&ctx);return -1;}
    if(elf.e_ident[EI_CLASS]!=ELFCLASS64){execve_error(pd,ip,&ctx);return -1;}

    for(usize i=0,off=elf.e_phoff;i<elf.e_phnum;i++,off+=sizeof(Elf64_Phdr)){
        Elf64_Phdr ph;
        if(inodes.read(ip,(u8*)&ph,off,sizeof(Elf64_Phdr))!=sizeof(Elf64_Phdr)){execve_error(pd,ip,&ctx);return -1;}
        if(ph.p_type!=PT_LOAD)continue;

        u64 sec_flag=0,end=0;
        if(ph.p_flags==(PF_R|PF_X)){
            sec_flag=ST_TEXT;
            end=ph.p_vaddr+ph.p_filesz;
        }
        else if(ph.p_flags==(PF_R|PF_W)){
            sec_flag=ST_FILE;
            end=ph.p_vaddr+ph.p_memsz;
        }
        else{execve_error(pd,ip,&ctx);return -1;}

        struct section* sec=kalloc(sizeof(struct section));
        memset(sec,0,sizeof(struct section));
        sec->begin=ph.p_vaddr;
        sec->end=end;
        sec->flags=sec_flag;
        _insert_into_list(&pd->section_head,&sec->stnode);

        u64 va=ph.p_vaddr,ph_off=ph.p_offset;
        while(va<ph.p_vaddr+ph.p_filesz){
            u64 va0=PAGE_BASE(va);
            u64 sz=MIN(PAGE_SIZE-(va-va0),ph.p_vaddr+ph.p_filesz-va);

            void *p=kalloc_page();
            memset(p,0,PAGE_SIZE);
            u64 pte_flag=PTE_USER_DATA;
            if(sec_flag==ST_TEXT)pte_flag|=PTE_RO;
            vmmap(pd,va0,p,pte_flag);
            
            if(inodes.read(ip,(u8*)p+va-va0,ph_off,sz)!=sz){
                execve_error(pd,ip,&ctx);
                return -1;
            }

            // for(usize i=0;i<sz;++i)printk("%c",*((u8*)p+va-va0+i));

            va+=sz;
            ph_off+=sz;
        }

        if(va!=PAGE_BASE(va))va=PAGE_BASE(va)+PAGE_SIZE;

        if(sec_flag==ST_FILE&&ph.p_memsz>va-ph.p_vaddr){
            while(va<ph.p_vaddr+ph.p_memsz){
                u64 va0=PAGE_BASE(va);
                u64 sz=MIN(PAGE_SIZE-(va-va0),ph.p_vaddr+ph.p_memsz-va);
                vmmap(pd,va0,get_zero_page(),PTE_USER_DATA|PTE_RO);
                va+=sz;
            }
        }
    }
    
    inodes.unlock(ip);
    inodes.put(&ctx,ip);
    bcache.end_op(&ctx);

    u64 sp=USERTOP;
    for(int i=1;i<=STACK_PAGE_SIZE;++i){
        void* p=kalloc_page();
        memset(p,0,PAGE_SIZE);
        vmmap(pd,sp-i*PAGE_SIZE,p,PTE_USER_DATA);
    }

    struct section *sec=kalloc(sizeof(struct section));
    memset(sec,0,sizeof(struct section));
    sec->flags=ST_STK;
    sec->begin=sp-STACK_PAGE_SIZE*PAGE_SIZE;
    sec->end=sp;
    _insert_into_list(&pd->section_head,&sec->stnode);

    Proc* this_proc=thisproc();
    
    u64 argc=0,envc=0;
    if(envp)while(envp[envc])++envc;
    if(argv)while(argv[argc])++argc;
    uint64_t newargv[argc+1],newenvp[envc+1];

    sp-=16;
    copyout(pd,(void*)sp,0,8);

    if(envp){
        for(int i=envc-1;i>=0;--i){
            sp-=strlen(envp[i])+1;
            sp-=sp%8;
            copyout(pd,(void*)sp,envp[i],strlen(envp[i])+1);
            newenvp[i]=sp;
        }
    }
    newenvp[envc]=0;

    sp-=8;
    copyout(pd,(void*)sp,0,8);

    if(argv){
        for(int i=argc-1;i>=0;--i){
            sp-=strlen(argv[i])+1;
            sp-=sp%8;
            copyout(pd,(void*)sp,argv[i],strlen(argv[i])+1);
            newargv[i]=sp;
        }
    }
    newargv[argc]=0;

    sp-=(u64)(envc+1)*8;
    copyout(pd,(void*)sp,newenvp,(u64)(envc+1)*8);

    sp-=(u64)(argc+1)*8;
    copyout(pd,(void*)sp,newargv,(u64)(argc+1)*8);

	sp-=8;
    copyout(pd, (void*)sp, &argc, sizeof(argc));

	free_pgdir(&this_proc->pgdir);
	this_proc->ucontext->sp = sp;
	this_proc->ucontext->elr = elf.e_entry;

    this_proc->pgdir=*pd;
    pd->section_head.prev->next=&this_proc->pgdir.section_head;
    pd->section_head.next->prev=&this_proc->pgdir.section_head;

	kfree(pd);
	attach_pgdir(&(this_proc->pgdir));
    arch_tlbi_vmalle1is();

	return 0;

    /* (Final) TODO END */
}
