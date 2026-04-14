// Copyright (C) 2024  ilobilo

export module x86_64.drivers.timers.tsc;
import std;

export namespace x86_64::timers::tsc
{
    bool supported();

    extern "C++" std::uint64_t rdtsc();
    std::uint64_t time_ns();

    void init();
    void finalise();
} // export namespace x86_64::timers::tsc