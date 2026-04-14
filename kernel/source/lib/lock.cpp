// Copyright (C) 2024-2026  ilobilo

module lib;

import system.sched;
import system.cpu.local;
import system.chrono;
import arch;
import std;

namespace lib::lock
{
    namespace
    {
        cpu_local(std::atomic_size_t, irq_depth, 0uz);
        cpu_local(bool, irq_status);
    } // namespace

    void acquire_irq()
    {
        const auto ret = arch::int_status();

        acquire_preempt();
        irq_status.unsafe_get() = ret;
        if (cpu::self().unsafe_get().in_interrupt.load(std::memory_order_acquire))
        {
            release_preempt();
            return;
        }

        if (irq_depth.unsafe_get().fetch_add(1, std::memory_order_acquire) == 0)
            arch::int_switch(false);

        release_preempt();
        return;
    }

    void release_irq()
    {
        acquire_preempt();
        if (cpu::self().unsafe_get().in_interrupt.load(std::memory_order_acquire))
        {
            release_preempt();
            return;
        }

        if (irq_depth.unsafe_get().fetch_sub(1, std::memory_order_release) == 1)
            arch::int_switch(irq_status.unsafe_get());

        release_preempt();
    }

    void acquire_preempt()
    {
        sched::preempt_disable();
    }

    void release_preempt()
    {
        sched::preempt_enable();
    }

    void pause() { arch::pause(); }

    std::uint64_t time()
    {
        return chrono::now(chrono::monotonic).to_ns();
    }
} // namespace lib::lock
