// Copyright (C) 2024-2025  ilobilo

export module drivers.timers;

export import drivers.timers.acpipm;
export import arch.drivers.timers;

import lib;

export namespace timers
{
    auto calibrator()
    {
        if (timers::acpipm::supported())
            return timers::acpipm::calibrate;

        return timers::arch::calibrator();
    }

    initgraph::stage *available_stage();
    initgraph::stage *should_init_stage();

    namespace arch
    {
        initgraph::stage *initialised_stage();
    } // namespace arch
} // export namespace timers