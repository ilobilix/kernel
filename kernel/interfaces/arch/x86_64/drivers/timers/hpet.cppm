// Copyright (C) 2024-2025  ilobilo

export module x86_64.drivers.timers.hpet;
import cppstd;

export namespace x86_64::timers::hpet
{
    bool initialised = false;
    std::size_t frequency;

    bool supported();

    std::uint64_t time_ns();
    void calibrate(std::size_t ms);

    void init();
} // export namespace x86_64::timers::hpet