// Copyright (C) 2024-2025  ilobilo

module system.scheduler;

import lib;
import cppstd;

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

    void finalise(const std::shared_ptr<process> &proc, const std::shared_ptr<thread> &thread, std::uintptr_t ip, std::uintptr_t arg)
    {
        lib::unused(proc, thread, ip, arg);
    }

    void deinitialise(process *proc, thread *thread)
    {
        lib::unused(proc, thread);
    }

    void save(const std::shared_ptr<thread> &thread)
    {
        lib::unused(thread);
    }

    void load(const std::shared_ptr<thread> &thread)
    {
        lib::unused(thread);
    }
} // namespace sched::arch