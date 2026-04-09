// Copyright (C) 2024-2026  ilobilo

export module system.sched:arch;

import std;

export namespace sched
{
    struct thread_t;
} // export namespace sched

export namespace sched::arch
{
    struct context
    {
    };

    struct data
    {
    };

    thread_t *current_thread();

    void context_switch(thread_t *prev, thread_t *next);

    void init_thread(
        thread_t *thread, std::uintptr_t ip, std::uintptr_t arg,
        std::uintptr_t stack_top, bool is_kernel
    );

    [[noreturn]] void return_to_user(std::uintptr_t ip, std::uintptr_t stack);
} // export namespace sched::arch
