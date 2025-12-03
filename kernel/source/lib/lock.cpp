// Copyright (C) 2024-2025  ilobilo

module lib;

import system.scheduler;
import system.cpu.self;
import system.time;
import arch;
import std;

namespace lib::lock
{
    namespace
    {
        cpu_local<std::atomic_size_t> irq_depth;
        cpu_local_init(irq_depth, 0uz);
    } // namespace

    bool acquire_irq()
    {
        const auto ret = arch::int_status();
        if (!cpu::local::available())
            return ret;

        if (cpu::self()->in_interrupt.load(std::memory_order_acquire))
            return ret;

        if (irq_depth->fetch_add(1, std::memory_order_acquire) == 0)
            arch::int_switch(false);
        return ret;
    }

    void release_irq(bool old)
    {
        if (!cpu::local::available())
        {
            arch::int_switch(old);
            return;
        }

        if (cpu::self()->in_interrupt.load(std::memory_order_acquire))
            return;

        if (irq_depth->fetch_sub(1, std::memory_order_release) == 1)
            arch::int_switch(old);
    }

    void acquire_preempt() { sched::disable(); }
    void release_preempt() { sched::enable(); }

    void pause() { arch::pause(); }

    // auto clock() -> std::uint64_t (*)();
    std::uint64_t (*clock())()
    {
        const auto clock = time::main_clock();
        if (clock == nullptr)
            return nullptr;
        return clock->ns;
    }
} // namespace lib::lock