// Copyright (C) 2024-2025  ilobilo

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

    void reschedule(std::size_t ms)
    {
        lib::unused(ms);
    }

    void finalise(process *proc, thread *thread, std::uintptr_t ip)
    {
        lib::unused(proc, thread, ip);
    }

    void deinitialise(process *proc, thread *thread)
    {
        lib::unused(proc, thread);
    }

    void save(thread *thread)
    {
        lib::unused(thread);
    }

    void load(thread *thread)
    {
        lib::unused(thread);
    }

    void update_stack(thread *thread, std::uintptr_t addr)
    {
        lib::unused(thread, addr);
    }
} // namespace sched::arch