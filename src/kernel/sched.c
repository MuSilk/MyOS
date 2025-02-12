#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <common/rbtree.h>

extern bool panic_flag;

extern void swtch(KernelContext *new_ctx, KernelContext **old_ctx);

static SpinLock schlock;
static ListNode schqueue;

void sched_timer_handler(struct timer* timer){
    timer->data=0;
    acquire_sched_lock();
    sched(RUNNABLE);
};


void init_sched()
{

    // TODO: initialize the scheduler
    // 1. initialize the resources (e.g. locks, semaphores)
    // 2. initialize the scheduler info of each CPU
    init_spinlock(&schlock);
    init_list_node(&schqueue);
    for(int i=0;i<NCPU;i++){
        Proc* p=create_proc();
        p->idle=1;
        p->state=RUNNING;
        cpus[i].sched.idle=p;
        cpus[i].sched.thisproc=p;

        cpus[i].sched.sched_timer.triggered=1;
        cpus[i].sched.sched_timer.data=i;
        cpus[i].sched.sched_timer.elapse=5;
        cpus[i].sched.sched_timer.handler=&sched_timer_handler;
    }

}

Proc *thisproc()
{
    // TODO: return the current process
    return cpus[cpuid()].sched.thisproc;
}

void init_schinfo(struct schinfo *p)
{
    // TODO: initialize your customized schinfo for every newly-created process
    init_list_node(&p->ptnode);
}

void acquire_sched_lock()
{
    // TODO: acquire the sched_lock if need
    acquire_spinlock(&schlock);
}

void release_sched_lock()
{
    // TODO: release the sched_lock if need
    release_spinlock(&schlock);
}

bool is_zombie(Proc *p)
{
    bool r;
    acquire_sched_lock();
    r = p->state == ZOMBIE;
    release_sched_lock();
    return r;
}

bool is_unused(Proc *p)
{
    bool r;
    acquire_sched_lock();
    r = p->state == UNUSED;
    release_sched_lock();
    return r;
}

bool _activate_proc(Proc *p, bool onalert)
{
    // TODO:(Lab5 new)
    // if the proc->state is RUNNING/RUNNABLE, do nothing and return false
    // if the proc->state is SLEEPING/UNUSED, set the process state to RUNNABLE, add it to the sched queue, and return true
    // if the proc->state is DEEPSLEEPING, do nothing if onalert or activate it if else, and return the corresponding value.
    acquire_sched_lock();

    if (p->state==RUNNING||p->state==RUNNABLE||(p->state==DEEPSLEEPING&&onalert)){
        release_sched_lock();
        return false;
    }
    
    if(p->state==SLEEPING||p->state==UNUSED||(p->state==DEEPSLEEPING&&!onalert)){
        p->state=RUNNABLE;
        _insert_into_list(&schqueue,&p->schinfo.ptnode);
        release_sched_lock();
        return true;
    }
    PANIC();
    return false;
}

static void update_this_state(enum procstate new_state)
{
    // TODO: if you use template sched function, you should implement this routinue
    // update the state of current process to new_state, and modify the sched queue if necessary
    Proc* this=thisproc();
    if(this!=cpus[cpuid()].sched.idle&&(this->state==RUNNING||this->state==RUNNABLE)){
        _detach_from_list(&this->schinfo.ptnode);
    }
    this->state=new_state;
    if(this!=cpus[cpuid()].sched.idle&&(this->state==RUNNING||this->state==RUNNABLE)){
        _insert_into_list(&schqueue,&this->schinfo.ptnode);
    }
}

static Proc *pick_next()
{
    // TODO: if using template sched function, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process

    if(_empty_list(&schqueue))return cpus[cpuid()].sched.idle;
    Proc* ret=cpus[cpuid()].sched.idle;
    for(ListNode* p=schqueue.prev;p!=&schqueue;p=p->prev){
        Proc* proc=container_of(p,Proc,schinfo.ptnode);
        if(proc->state==RUNNABLE){
            ret=proc;
            break;
        }
    }
    return ret;
}

static void update_this_proc(Proc *p)
{
    // TODO: you should implement this routinue
    // update thisproc to the choosen process
    cpus[cpuid()].sched.thisproc=p;

    if(!cpus[cpuid()].sched.sched_timer.triggered){
        cancel_cpu_timer(&cpus[cpuid()].sched.sched_timer);
    }
    set_cpu_timer(&cpus[cpuid()].sched.sched_timer);
}

// A simple scheduler.
// You are allowed to replace it with whatever you like.
// call with sched_lock
void sched(enum procstate new_state)
{
    auto this = thisproc();
    ASSERT(this->state == RUNNING);
    if(this->killed&&new_state!=ZOMBIE){
        release_sched_lock();
        return;
    }
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    if (next != this) {
        attach_pgdir(&next->pgdir);
        swtch(next->kcontext, &this->kcontext);
    }
    release_sched_lock();
}

u64 proc_entry(void (*entry)(u64), u64 arg)
{
    release_sched_lock();
    set_return_addr(entry);
    return arg;
}
