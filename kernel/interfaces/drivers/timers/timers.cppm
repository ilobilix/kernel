// Copyright (C) 2024-2025  ilobilo

export module drivers.timers;

export import drivers.timers.acpipm;
export import arch.drivers.timers;

import lib;
import cppstd;

export namespace timers
{
    auto calibrator() -> std::size_t (*)(std::size_t ms)
    {
        if (const auto ret = timers::arch::calibrator())
            return ret;
        else if (timers::acpipm::supported())
            return timers::acpipm::calibrate;
        return nullptr;
    }

    initgraph::stage *available_stage();
    initgraph::stage *should_init_stage();

    namespace arch
    {
        initgraph::stage *initialised_stage();
    } // namespace arch
} // export namespace timers