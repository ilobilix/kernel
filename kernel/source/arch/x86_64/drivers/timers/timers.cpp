// Copyright (C) 2024-2025  ilobilo

module arch.drivers.timers;

import drivers.timers;
import lib;

namespace timers::arch
{
    using namespace x86_64::timers;

    template<auto Func>
    void use_timer(std::size_t ms)
    {
        auto end = Func() + (ms * 1'000'000);
        while (Func() < end) { }
    }

    auto calibrator() -> void (*)(std::size_t ms)
    {
        if (kvm::supported())
            return use_timer<kvm::time_ns>;
        else if (hpet::is_initialised())
            return hpet::calibrate;
        else if (pit::is_initialised())
            return use_timer<pit::time_ns>;

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