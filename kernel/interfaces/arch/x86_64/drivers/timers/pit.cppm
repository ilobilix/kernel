// Copyright (C) 2024-2025  ilobilo

export module x86_64.drivers.timers.pit;

import lib;
import std;

export namespace x86_64::timers::pit
{
    constexpr std::size_t frequency = 1'000;

    bool is_initialised();
    std::uint64_t time_ns();

    lib::initgraph::stage *initialised_stage();
} // export namespace x86_64::timers::pit