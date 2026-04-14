// Copyright (C) 2024-2026  ilobilo

module system.scheduler;

import lib;
import std;

namespace sched
{
    void schedule(cpu::registers *regs);
} // namespace sched

namespace sched::arch
{
    void init()
    {
    }

    void enable()
    {
    }

    void disable()
    {
    }

    void reschedule(std::size_t ms)
    {
        lib::unused(ms);
    }

    void reschedule_other(std::size_t cpu)
    {
        lib::unused(cpu);
    }

    void initialise(process *proc, thread *thread, std::uintptr_t ip, std::uintptr_t arg)
    {
        lib::unused(proc, thread, ip, arg);
    }

    void deinitialise(process *proc, thread *thread)
    {
        lib::unused(proc, thread);
    }

    [[noreturn]]
    void enter_user(thread *thread, std::uintptr_t ip, std::uintptr_t stack)
    {
        lib::unused(thread, ip, stack);
        lib::panic("enter_user not implemented");
        std::unreachable();
    }

    void save(thread *thread)
    {
        lib::unused(thread);
    }

    void load(thread *thread)
    {
        lib::unused(thread);
    }

    void ctx_switch(thread *current, thread *next)
    {
        lib::unused(current, next);
    }
} // namespace sched::arch
