// Copyright (C) 2024-2025  ilobilo

module arch.drivers.timers;

import drivers.timers;
import lib;

namespace timers::arch
{
    using namespace x86_64::timers;

    template<auto Func>
    std::size_t use_timer(std::size_t ms)
    {
        const auto start = Func();
        const auto end = start + (ms * 1'000'000);
        while (Func() < end) { }
        return Func() - start;
    }

    auto calibrator() -> std::size_t (*)(std::size_t ms)
    {
        if (kvm::supported())
            return use_timer<kvm::time_ns>;
        else if (hpet::is_initialised())
            return hpet::calibrate;
        // else if (pit::is_initialised())
        //     return use_timer<pit::time_ns>;

        return nullptr;
    }

    initgraph::task timers_task
    {
        "arch.initialise-timers",
        initgraph::require { should_init_stage() },
        initgraph::entail { initialised_stage() },
        [] {
            pit::init();
            hpet::init();
            kvm::init();
            tsc::init();
        }
    };
} // expo namespace timers::arch