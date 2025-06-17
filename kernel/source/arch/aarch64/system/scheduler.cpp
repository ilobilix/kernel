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

    void finalise(std::shared_ptr<process> &proc,std::shared_ptr<thread> &thread, std::uintptr_t ip, std::uintptr_t arg)
    {
        lib::unused(proc, thread, ip, arg);
    }

    void deinitialise(std::shared_ptr<process> &proc, thread *thread)
    {
        lib::unused(proc, thread);
    }

    void save(std::shared_ptr<thread> &thread)
    {
        lib::unused(thread);
    }

    void load(std::shared_ptr<thread> &thread)
    {
        lib::unused(thread);
    }
} // namespace sched::arch