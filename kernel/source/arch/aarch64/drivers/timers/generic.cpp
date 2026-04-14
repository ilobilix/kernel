// Copyright (C) 2024-2025  ilobilo

module aarch64.drivers.timers.generic;

import system.cpu.self;
import system.cpu;
import system.time;

import lib;
import cppstd;

namespace aarch64::timers::generic
{
    // TODO

    std::uint64_t time_ns()
    {
        return 0;
    }

    time::clock clock { "generic", 0, time_ns };
    void init()
    {
        time::register_clock(clock);
    }
} // namespace aarch64::timers::generic