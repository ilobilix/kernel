// Copyright (C) 2024-2025  ilobilo

module drivers.timers;

import arch;
import lib;

namespace timers
{
    initgraph::stage *should_init_stage()
    {
        static initgraph::stage stage { "can-init-timers" };
        return &stage;
    }

    initgraph::stage *available_stage()
    {
        static initgraph::stage stage { "timers-available" };
        return &stage;
    }

    initgraph::task can_timers_task
    {
        "set-can-init-timers",
        initgraph::require { acpipm::available_stage(), ::arch::bsp_stage() },
        initgraph::entail { should_init_stage() },
        [] { }
    };

    initgraph::task timers_task
    {
        "timers-initialised",
        initgraph::require { arch::initialised_stage() },
        initgraph::entail { available_stage() },
        [] { }
    };

    namespace arch
    {
        initgraph::stage *initialised_stage()
        {
            static initgraph::stage stage { "arch.timers-initialised" };
            return &stage;
        }
    } // namespace arch
} // namespace timers