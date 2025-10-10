// Copyright (C) 2024-2025  ilobilo

module arch.drivers.timers;

import drivers.timers;
import lib;

namespace timers::arch
{
    using namespace aarch64::timers;

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