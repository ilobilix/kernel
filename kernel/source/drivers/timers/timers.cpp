// Copyright (C) 2024-2025  ilobilo

module drivers.timers;

import arch;
import lib;

namespace timers
{
    lib::initgraph::stage *initialised_stage()
    {
        static lib::initgraph::stage stage
        {
            "timers.initialised",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task timers_task
    {
        "timers.initialise",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { timers::arch::initialised_stage() },
        lib::initgraph::entail { initialised_stage() },
        [] { }
    };
} // namespace timers