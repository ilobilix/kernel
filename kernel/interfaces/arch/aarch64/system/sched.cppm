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

    void init_core(thread_t *initial);
    void init_thread(
        thread_t *thread, std::uintptr_t ip, std::uintptr_t arg,
        bool is_trampoline, bool is_clone
    );
    void deinit_thread(thread_t *thread);

    void arm_timer_ns(std::uint64_t ns);
    void wake_up_other(std::size_t cpu_idx);

    void context_switch(thread_t *prev, thread_t *next);
    [[noreturn]] void return_to_user(std::uintptr_t ip, std::uintptr_t stack);
} // export namespace sched::arch
