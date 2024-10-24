#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <aarch64/mmu.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>

// #define _DEBUG

Proc root_proc;
static SpinLock proclock;

typedef struct _pidNode{
    int pid;
    struct _pidNode* next;
}pidNode;
pidNode* pid_head=NULL;
int pid_tail=0;
static void free_pid(int pid){
    pidNode* p=kalloc(sizeof(pidNode));
    p->pid=pid;
    p->next=pid_head;
    pid_head=p;
}
static int fetch_pid(){
    if(pid_head==NULL)return ++pid_tail;
    int pid=pid_head->pid;
    pidNode* p=pid_head;
    pid_head=pid_head->next;
    kfree(p);
    return pid;
}

void print_proc_tree(Proc* root){
    if(_empty_list(&root->children))return;
    _for_in_list(p,&root->children){
        if(p==&root->children)break;
        Proc* childp=container_of(p,Proc,ptnode);
        printk("%d->%d\n",root->pid,childp->pid);
        ASSERT(childp->pid>0);
        print_proc_tree(childp);
    }
}

void kernel_entry();
void proc_entry();

// init_kproc initializes the kernel process
// NOTE: should call after kinit
void init_kproc()
{
    // TODO:
    // 1. init global resources (e.g. locks, semaphores)
    // 2. init the root_proc (finished)

    init_spinlock(&proclock);

    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
    // printk("root_proc addr:%llx\n",(u64)&root_proc);
}

void init_proc(Proc *p)
{
    // TODO:
    // setup the Proc with kstack and pid allocated
    // NOTE: be careful of concurrency

    acquire_spinlock(&proclock);
    p->killed=0;
    p->idle=0;
    p->pid=fetch_pid();
    // p->exitcode=0;
    p->state=UNUSED;
    init_sem(&p->childexit,0);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    p->parent=NULL;
    init_schinfo(&p->schinfo);
    init_pgdir(&p->pgdir);
    p->kstack=kalloc_page();
    p->ucontext=(UserContext*)((u64)p->kstack+PAGE_SIZE-16-sizeof(UserContext));
    p->kcontext=(KernelContext*)((u64)p->kstack+PAGE_SIZE-16-sizeof(KernelContext)-sizeof(UserContext));
    release_spinlock(&proclock);
}

Proc *create_proc()
{
    Proc *p = kalloc(sizeof(Proc));
    init_proc(p);
    return p;
}

void set_parent_to_this(Proc *proc)
{
    // TODO: set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL
#ifdef _DEBUG
    printk("set %d parent %d\n",proc->pid,thisproc()->pid);
#endif

    acquire_spinlock(&proclock);
    Proc* this=thisproc();
    proc->parent=this;
    _insert_into_list(&this->children,&proc->ptnode);
#ifdef _DEBUG
    printk("----------\n");
    print_proc_tree(&root_proc);
    printk("----------\n");
#endif
    release_spinlock(&proclock);
}

int start_proc(Proc *p, void (*entry)(u64), u64 arg)
{
    // TODO:
    // 1. set the parent to root_proc if NULL
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    // 3. activate the proc and return its pid
    // NOTE: be careful of concurrency

#ifdef _DEBUG
    printk("proc start %d\n",p->pid);
#endif

    acquire_spinlock(&proclock);
    if(p->parent==NULL){
        p->parent=&root_proc;
        _insert_into_list(&root_proc.children,&p->ptnode);
    }
    p->kcontext->lr=(u64)&proc_entry;
    p->kcontext->x0=(u64)entry;
    p->kcontext->x1=(u64)arg;
    int pid=p->pid;
    activate_proc(p);
    release_spinlock(&proclock);
    return pid;
}

int wait(int *exitcode)
{
    // TODO:
    // 1. return -1 if no children
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its pid and exitcode
    // NOTE: be careful of concurrency

#ifdef _DEBUG
    printk("proc wait %d,root state %d\n",thisproc()->pid,root_proc.state);

    printk("----------0\n");
    print_proc_tree(&root_proc);
    printk("----------\n");
#endif

    acquire_spinlock(&proclock);
    Proc* this=thisproc();
    if(_empty_list(&this->children)){
        release_spinlock(&proclock);
        return -1;
    }
    release_spinlock(&proclock);

    if(!wait_sem(&this->childexit)){
        return -1;
    }

    acquire_spinlock(&proclock);
    acquire_sched_lock();
    _for_in_list(p,&this->children){
        if(p==&this->children)break;
        Proc* childproc=container_of(p,struct Proc,ptnode); 
        if(childproc->state==ZOMBIE){
            _detach_from_list(&childproc->ptnode);
            _detach_from_list(&childproc->schinfo.ptnode);

            *exitcode=childproc->exitcode;
            kfree_page(childproc->kstack);
            int pid=childproc->pid;
            kfree(childproc);
            free_pid(pid);

            #ifdef _DEBUG
                printk("proc wait %d,root state... %d\n",thisproc()->pid,root_proc.state);
                printk("----------2\n");
                print_proc_tree(&root_proc);
                printk("----------\n");
            #endif

            release_sched_lock();
            release_spinlock(&proclock);

            return pid;
        }
    }
    release_sched_lock();
    release_spinlock(&proclock);
    return -1;
}

NO_RETURN void exit(int code)
{
    // TODO:
    // 1. set the exitcode
    // 2. clean up the resources
    // 3. transfer children to the root_proc, and notify the root_proc if there is zombie
    // 4. sched(ZOMBIE)
    // NOTE: be careful of concurrency

    acquire_spinlock(&proclock);
    acquire_sched_lock();

    Proc* this=thisproc();
    this->exitcode=code;
    if(!_empty_list(&this->children)){
        _for_in_list(p,&this->children){
            if(p==&this->children)break;
            Proc* childproc=container_of(p,Proc,ptnode);
            childproc->parent=&root_proc;
        }
        _merge_list(&root_proc.children,this->children.next);
        _detach_from_list(&this->children);        
    }
    free_pgdir(&this->pgdir);

    release_sched_lock();
    if(this->childexit.val>0)for(int i=0;i<this->childexit.val;i++)post_sem(&root_proc.childexit);
    post_sem(&this->parent->childexit);

    release_spinlock(&proclock);
    acquire_sched_lock();
    sched(ZOMBIE);
    PANIC(); // prevent the warning of 'no_return function returns'
}

Proc* find_proc(int pid,Proc* root){
    if(root->pid==pid)return root;
    _for_in_list(p,&root->children){
        if(p==&root->children)break;
        Proc* proc=container_of(p,Proc,ptnode);
        Proc* tmp=find_proc(pid,proc);
        if(tmp!=NULL)return tmp;
    }
    return NULL;
}

int kill(int pid)
{
    // TODO:
    // Set the killed flag of the proc to true and return 0.
    // Return -1 if the pid is invalid (proc not found).

    acquire_spinlock(&proclock);
    Proc* p=find_proc(pid,&root_proc);
    if(p!=NULL&&!is_unused(p)){
        p->killed=1;
        activate_proc(p);
        release_spinlock(&proclock);
        return 0;
    }
    release_spinlock(&proclock);
    return -1;
}