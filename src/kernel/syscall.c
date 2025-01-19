#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/sem.h>
#include <test/test.h>
#include <aarch64/intrinsic.h>
#include <kernel/paging.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"

void init_syscall()
{
    for (u64 *p = (u64 *)&early_init; p < (u64 *)&rest_init; p++)
        ((void (*)()) * p)();
}

void *syscall_table[NR_SYSCALL] = {
    [0 ... NR_SYSCALL - 1] = NULL,
    [SYS_myreport] = (void *)syscall_myreport,
};

void syscall_entry(UserContext *context)
{
    // TODO
    // Invoke syscall_table[id] with args and set the return value.
    // id is stored in x8. args are stored in x0-x5. return value is stored in x0.
    // be sure to check the range of id. if id >= NR_SYSCALL, panic.

    // for(int i=0;i<18;i++)printk("%lld ",context->x[i]);
    // printk("\n");

    u64 id=context->x[8];
    // printk("id:%lld syscall_table[id]:%llx\n",id,(u64)syscall_table[id]);
    if(id<NR_SYSCALL&&syscall_table[id]!=NULL){
        u64 res=((u64(*)(u64,u64,u64,u64,u64,u64))syscall_table[id])
            (context->x[0],context->x[1],context->x[2],context->x[3],context->x[4],context->x[5]);
        context->x[0]=res;
    }
    else PANIC();
}

/** 
 * Check if the virtual address [start,start+size) is READABLE by the current
 * user process.
 */
bool user_readable(const void *start, usize size) {
    /* (Final) TODO BEGIN */
    if((u64)start>=KSPACE_MASK)return true;
    auto st_head=&thisproc()->pgdir.section_head;
    _for_in_list(p,st_head){
        if(p==st_head)break;
        auto sec=container_of(p,struct section,stnode);
        if(sec->begin<=(u64)start&&(u64)start+size<=sec->end)return true;
    }
    return false;
    /* (Final) TODO END */
}


/**
 * Check if the virtual address [start,start+size) is READABLE & WRITEABLE by
 * the current user process.
 */
bool user_writeable(const void *start, usize size) {
    /* (Final) TODO Begin */
    if((u64)start>=KSPACE_MASK)return true;
    auto st_head=&thisproc()->pgdir.section_head;
    _for_in_list(p,st_head){
        if(p==st_head)break;
        auto sec=container_of(p,struct section,stnode);
        if(!(sec->flags&ST_RO)&&sec->begin<=(u64)start&&(u64)start+size<=sec->end)return true;
    }
    return false;
    /* (Final) TODO End */
}

/** 
 * Get the length of a string including tailing '\0' in the memory space of
 * current user process return 0 if the length exceeds maxlen or the string is
 * not readable by the current user process.
 */
usize user_strlen(const char *str, usize maxlen) {
    for (usize i = 0; i < maxlen; i++) {
        if (user_readable(&str[i], 1)) {
            if (str[i] == 0)
                return i + 1;
        } else
            return 0;
    }
    return 0;
}