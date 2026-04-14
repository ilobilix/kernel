// Copyright (C) 2024-2025  ilobilo

export module x86_64.drivers.timers.pit;
import std;

export namespace x86_64::timers::pit
{
    constexpr std::size_t frequency = 1'000;
    bool initialised = false;

    std::uint64_t time_ns();
    void init();
} // export namespace x86_64::timers::pit