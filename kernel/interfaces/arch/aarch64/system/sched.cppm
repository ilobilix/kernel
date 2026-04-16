// Copyright (C) 2024-2026  ilobilo

export module system.sched:arch;

import system.cpu.regs;
import std;

import :signal;

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

    struct mcontext_t
    {
    };

    struct ucontext_t
    {
        std::uint64_t uc_flags;
        ucontext_t *uc_link;
        stack_t uc_stack;
        mcontext_t uc_mcontext;
        sigset_t uc_sigmask;
    };

    struct sigframe_t
    {
        std::uintptr_t pretcode;
        ucontext_t uc;
        siginfo_t info;
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

    bool in_user_mode(const cpu::registers *regs);
    bool setup_sigframe(
        thread_t *thread, cpu::registers *regs,
        int sig, const siginfo_t &info, const sigaction_t &action
    );
} // export namespace sched::arch
