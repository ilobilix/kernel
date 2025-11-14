// Copyright (C) 2024-2025  ilobilo

module lib;

import system.scheduler;
import system.cpu.self;
import system.time;
import arch;
import cppstd;

namespace lib::lock
{
    namespace
    {
        cpu_local<std::atomic_size_t> irq_depth;
        cpu_local_init(irq_depth, 0uz);

        std::atomic_size_t prepcpu_depth = 0;
    } // namespace


    bool acquire_irq()
    {
        const auto ret = arch::int_status();

        std::atomic_size_t *depth = &prepcpu_depth;
        if (cpu::percpu_available())
        {
            depth = &irq_depth.get();
            if (cpu::self()->in_interrupt.load(std::memory_order_acquire))
                return ret;
        }

        if (depth->fetch_add(1, std::memory_order_acquire) == 0)
            arch::int_switch(false);
        return ret;
    }

    void release_irq(bool old)
    {
        std::atomic_size_t *depth = &prepcpu_depth;
        if (cpu::percpu_available())
        {
            depth = &irq_depth.get();
            if (cpu::self()->in_interrupt.load(std::memory_order_acquire))
                return;
        }

        if (depth->fetch_sub(1, std::memory_order_release) == 1)
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