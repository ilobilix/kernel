// Copyright (C) 2024-2025  ilobilo

module drivers.timers;

import arch;
import lib;

namespace timers
{
    initgraph::stage *available_stage()
    {
        static initgraph::stage stage { "timers-available" };
        return &stage;
    }

    initgraph::task timers_task
    {
        "initialise-timers",
        initgraph::require { acpipm::available_stage(), ::arch::bsp_stage() },
        initgraph::entail { available_stage() },
        [] {
            timers::arch::init();
        }
    };
} // namespace timers