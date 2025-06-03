// Copyright (C) 2024-2025  ilobilo

module lib;

import system.scheduler;
import system.time;
import arch;
import cppstd;

namespace lib::lock
{
    bool lock_ints()
    {
        auto ints = arch::int_status();
        arch::int_switch(false);
        return ints;
    }

    void unlock_ints(bool ints)
    {
        if (arch::int_status() != ints)
            arch::int_switch(ints);
    }

    bool lock_preempt()
    {
        auto preempt = sched::is_enabled();
        sched::disable();
        return preempt;
    }

    void unlock_preempt(bool preempt)
    {
        if (sched::is_enabled() != preempt)
        {
            if (preempt)
                sched::enable();
            else
                sched::disable();
        }
    }

    void pause()
    {
        arch::pause();
    }

    // auto clock() -> std::uint64_t (*)();
    std::uint64_t (*clock())()
    {
        const auto clock = time::main_clock();
        if (clock == nullptr)
            return nullptr;
        return clock->ns;
    }
} // namespace lib::lock