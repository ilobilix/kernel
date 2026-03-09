// Copyright (C) 2024-2026  ilobilo

export module x86_64.drivers.timers.pit;

import lib;
import std;

export namespace x86_64::timers::pit
{
    constexpr std::size_t frequency = 1'000;

    bool is_initialised();
    // std::size_t calibrate(std::size_t ms);

    lib::initgraph::stage *initialised_stage();
} // export namespace x86_64::timers::pit
