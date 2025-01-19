#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <test/test.h>
#include <driver/virtio.h>
#include <kernel/proc.h>
#include <kernel/paging.h>
#include <kernel/mem.h>

volatile bool panic_flag;
extern char icode[],eicode[];
void trap_return();

NO_RETURN void idle_entry()
{
    set_cpu_on();
    while (1) {
        yield();
        if (panic_flag)
            break;
        arch_with_trap
        {
            arch_wfi();
        }
    }
    PANIC();
    set_cpu_off();
    arch_stop_cpu();
}

NO_RETURN void kernel_entry()
{
    init_filesystem();

    printk("Hello world! (Core %lld)\n", cpuid());
    // proc_test();
    // vm_test();
    // user_proc_test();
    // io_test();

    /**
     * (Final) TODO BEGIN 
     * 
     * Map init.S to user space and trap_return to run icode.
     */

    auto p=create_proc();

    struct section* sec=kalloc(sizeof(struct section));
    sec->begin=0x400000;
    sec->end = 0x400000+(u64)eicode-(u64)icode;
    sec->flags=ST_TEXT;
    _insert_into_list(&p->pgdir.section_head,&sec->stnode);
    for(u64 i=(u64)icode;i<(u64)eicode;i+=PAGE_SIZE){
        *get_pte(&p->pgdir,0x400000+i-(u64)icode,true)=K2P(i)|PTE_USER_DATA;
    }

    p->ucontext->x[0]=0;
    p->ucontext->elr=0x400000;
    p->ucontext->spsr=0;
    OpContext ctx;
    bcache.begin_op(&ctx);
    p->cwd=namei("/",&ctx);
    bcache.end_op(&ctx);
    p->parent=thisproc();

    start_proc(p,trap_return,0);
    while(1){
        yield();
        arch_with_trap{
            arch_wfi();
        }
    }

    /* (Final) TODO END */
}

NO_INLINE NO_RETURN void _panic(const char *file, int line)
{
    printk("=====%s:%d PANIC%lld!=====\n", file, line, cpuid());
    while(1){}
    panic_flag = true;
    PANIC();
    set_cpu_off();
    for (int i = 0; i < NCPU; i++) {
        if (cpus[i].online)
            i--;
    }
    printk("Kernel PANIC invoked at %s:%d. Stopped.\n", file, line);
    arch_stop_cpu();
}