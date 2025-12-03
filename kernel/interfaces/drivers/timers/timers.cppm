// Copyright (C) 2024-2025  ilobilo

export module drivers.timers;

export import drivers.timers.acpipm;
export import arch.drivers.timers;

import lib;
import std;

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

    lib::initgraph::stage *initialised_stage();

    namespace arch
    {
        lib::initgraph::stage *can_initialise_stage();
        lib::initgraph::stage *initialised_stage();
    } // namespace arch
} // export namespace timers