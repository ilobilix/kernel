// Copyright (C) 2024-2026  ilobilo

module lib;

import system.scheduler;
import system.cpu.local;
import system.chrono;
import arch;
import std;

namespace lib::lock
{
    namespace
    {
        cpu_local(std::atomic_size_t, irq_depth, 0uz);
    } // namespace

    bool acquire_irq()
    {
        const auto ret = arch::int_status();
        if (!cpu::local::available())
            return ret;

        acquire_preempt();
        if (cpu::self().unsafe_get().in_interrupt.load(std::memory_order_acquire))
        {
            release_preempt();
            return ret;
        }

        if (irq_depth.unsafe_get().fetch_add(1, std::memory_order_acquire) == 0)
            arch::int_switch(false);

        release_preempt();
        return ret;
    }

    void release_irq(bool old)
    {
        if (!cpu::local::available())
        {
            arch::int_switch(old);
            return;
        }

        acquire_preempt();
        if (cpu::self().unsafe_get().in_interrupt.load(std::memory_order_acquire))
        {
            release_preempt();
            return;
        }

        if (irq_depth.unsafe_get().fetch_sub(1, std::memory_order_release) == 1)
            arch::int_switch(old);

        release_preempt();
    }

    void acquire_preempt()
    {
        sched::disable();
    }

    void release_preempt()
    {
        sched::enable();
    }

    void pause() { arch::pause(); }

    std::uint64_t time()
    {
        return chrono::now(chrono::monotonic).to_ns();
    }
} // namespace lib::lock
