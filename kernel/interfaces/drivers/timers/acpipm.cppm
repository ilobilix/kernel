// Copyright (C) 2024-2025  ilobilo

export module drivers.timers.acpipm;
import std;

export namespace timers::acpipm
{
    constexpr std::size_t frequency = 3579545;
    bool initialised = false;
    std::atomic_size_t overflows = 0;

    bool supported();

    std::uint64_t time_ns();
    void calibrate(std::size_t ms);

    void init();
    void finalise();
} // export namespace timers::acpipm