// Copyright (C) 2024-2025  ilobilo

module arch.drivers.timers;

import drivers.timers;
import lib;

namespace timers::arch
{
    using namespace aarch64::timers;

    template<auto Func>
    void use_timer(std::size_t ms)
    {
        auto end = Func() + (ms * 1'000'000);
        while (Func() < end) { }
    }

    auto calibrator() -> void (*)(std::size_t ms)
    {
        return use_timer<generic::time_ns>;
    }

    initgraph::task timers_task
    {
        "arch.initialise-timers",
        initgraph::require { should_init_stage() },
        initgraph::entail { initialised_stage() },
        [] {
            generic::init();
        }
    };
} // expo namespace timers::arch