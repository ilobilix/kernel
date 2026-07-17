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
        cpu_local(std::size_t, boot_irq_depth, 0uz);
        cpu_local(bool, boot_irq_status);
    } // namespace

    void acquire_irq()
    {
        const bool status = arch::int_status();
        arch::int_switch(false);

        if (cpu::self().unsafe_get().sched_ready.load(std::memory_order_relaxed)) [[likely]]
        {
            const auto thread = sched::current_thread();
            if (thread->irq_depth++ == 0)
                thread->irq_status = status;
        }
        else
        {
            auto &depth = boot_irq_depth.unsafe_get();
            if (depth++ == 0)
                boot_irq_status.unsafe_get() = status;
        }
    }

    void release_irq()
    {
        if (cpu::self().unsafe_get().sched_ready.load(std::memory_order_relaxed)) [[likely]]
        {
            const auto thread = sched::current_thread();
            bug_on(thread->irq_depth == 0);
            if (--thread->irq_depth == 0)
                arch::int_switch(thread->irq_status);
        }
        else
        {
            auto &depth = boot_irq_depth.unsafe_get();
            bug_on(depth == 0);
            if (--depth == 0)
                arch::int_switch(boot_irq_status.unsafe_get());
        }
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
