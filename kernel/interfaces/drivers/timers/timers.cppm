// Copyright (C) 2024-2025  ilobilo

export module drivers.timers;

export import drivers.timers.acpipm;
export import arch.drivers.timers;

import lib;

namespace timers
{
    export auto calibrator()
    {
        if (timers::acpipm::supported())
            return timers::acpipm::calibrate;

        return timers::arch::calibrator();
    }

    export initgraph::stage *available_stage();
} // export namespace timers