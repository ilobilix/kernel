// Copyright (C) 2024-2026  ilobilo

module system.sched;

import system.cpu.arch;

namespace sched::arch
{
    thread_t *current_thread()
    {
        return reinterpret_cast<thread_t *>(cpu::read_el1_base());
    }

    void init_core(thread_t *initial)
    {
        // TODO
        lib::unused(initial);
    }

    void init_thread(
        thread_t *thread, std::uintptr_t ip, std::uintptr_t arg,
        bool is_trampoline, bool is_clone
    )
    {
        // TODO
        lib::unused(thread, ip, arg, is_trampoline, is_clone);
    }

    void deinit_thread(thread_t *thread)
    {
        // TODO
        lib::unused(thread);
    }

    void arm_timer_ns(std::uint64_t ns)
    {
        // TODO
        lib::unused(ns);
    }

    void wake_up_other(std::size_t cpu_idx)
    {
        // TODO
        lib::unused(cpu_idx);
    }

    void context_switch(thread_t *prev, thread_t *next)
    {
        lib::unused(prev, next);
        // TODO
    }

    [[noreturn]] void return_to_user(std::uintptr_t ip, std::uintptr_t stack)
    {
        // TODO
        lib::unused(ip, stack);
        std::unreachable();
    }

    bool in_user_mode(const cpu::registers *regs)
    {
        // TODO
        lib::unused(regs);
        return false;
    }

    bool setup_sigframe(
        thread_t *thread, cpu::registers *regs,
        int sig, const siginfo_t &info, const sigaction_t &action
    )
    {
        // TODO
        lib::unused(thread, regs, sig, info, action);
        return false;
    }

    bool restore_sigframe(thread_t *thread, cpu::registers *regs)
    {
        // TODO
        lib::unused(thread, regs);
        return false;
    }
} // namespace sched::arch
