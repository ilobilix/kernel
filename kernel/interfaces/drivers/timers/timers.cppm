// Copyright (C) 2024  ilobilo

export module drivers.timers;

export import drivers.timers.acpipm;
export import arch.drivers.timers;

export namespace timers
{
    void init()
    {
        timers::acpipm::init();
        timers::arch::init();
    }
} // export namespace timers