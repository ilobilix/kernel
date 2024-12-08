// Copyright (C) 2024  ilobilo

export module drivers.timers;

export import drivers.timers.acpipm;
export import arch.drivers.timers;

import std;

export namespace timers
{
    auto calibrator()
    {
        if (timers::acpipm::supported())
            return timers::acpipm::calibrate;

        return timers::arch::calibrator();
    }

    void init()
    {
        timers::acpipm::init();
        timers::arch::init();
    }
} // export namespace timers