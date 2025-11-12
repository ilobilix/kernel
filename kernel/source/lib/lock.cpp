// Copyright (C) 2024-2025  ilobilo

module lib;

import system.scheduler;
import system.time;
import arch;
import cppstd;

namespace lib::lock
{
    bool acquire_irq()
    {
        auto irq = arch::int_status();
        arch::int_switch(false);
        return irq;
    }

    void release_irq(bool irq)
    {
        if (arch::int_status() != irq)
            arch::int_switch(irq);
    }

    bool in_interrupt() { return arch::in_interrupt(); }

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