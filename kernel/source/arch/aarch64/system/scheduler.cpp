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

    void finalise(std::shared_ptr<thread> thread, std::uintptr_t ip)
    {
        lib::unused(thread, ip);
    }

    void deinitialise(std::shared_ptr<thread> thread)
    {
        lib::unused(thread);
    }

    void save(std::shared_ptr<thread> thread)
    {
        lib::unused(thread);
    }

    void load(std::shared_ptr<thread> thread)
    {
        lib::unused(thread);
    }
} // namespace sched::arch