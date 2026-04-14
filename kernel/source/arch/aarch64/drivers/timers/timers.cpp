// Copyright (C) 2024-2026  ilobilo

module arch.drivers.timers;

import drivers.timers;
import lib;

namespace timers::arch
{
    using namespace aarch64::timers;

    auto calibrator() -> std::size_t (*)(std::size_t ms)
    {
        return nullptr;
    }

    lib::initgraph::task timers_task
    {
        "timers.arch",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { generic::initialised_stage() },
        lib::initgraph::entail { initialised_stage() },
        [] { }
    };
} // expo namespace timers::arch
