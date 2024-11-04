#pragma once

#include <kernel/proc.h>
#include <common/rbtree.h>

#define NCPU 4

struct timer {
    bool triggered;
    int elapse;
    u64 _key;
    struct rb_node_ _node;
    void (*handler)(struct timer *);
    u64 data;
};

struct sched {
    // TODO: customize your sched info
    Proc* thisproc;
    Proc* idle;
    struct timer sched_timer;
};

struct cpu {
    bool online;
    struct rb_root_ timer;
    struct sched sched;
};

extern struct cpu cpus[NCPU];

void init_clock_handler();

void set_cpu_on();
void set_cpu_off();

void set_cpu_timer(struct timer *timer);
void cancel_cpu_timer(struct timer *timer);