// Copyright (C) 2024-2026  ilobilo

module lib;

import system.sched;
import system.cpu.local;
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
        const bool status = arch::int_status();
        sched::preempt_disable();

        if (irq_depth.unsafe_get().fetch_add(1, std::memory_order_acquire) == 0)
        {
            irq_status.unsafe_get() = status;
            if (!cpu::self().unsafe_get().in_interrupt.load(std::memory_order_relaxed))
                arch::int_switch(false);
        }

        sched::preempt_enable();
    }

    void release_irq()
    {
        sched::preempt_disable();

        if (irq_depth.unsafe_get().fetch_sub(1, std::memory_order_release) == 1 &&
            !cpu::self().unsafe_get().in_interrupt.load(std::memory_order_relaxed))
            arch::int_switch(irq_status.unsafe_get());

        sched::preempt_enable();
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
} // namespace lib::lock
