// Copyright (C) 2024-2025  ilobilo

module aarch64.drivers.timers.generic;

import system.cpu.self;
import system.cpu;
import system.time;
import drivers.timers;

import lib;
import cppstd;

namespace aarch64::timers::generic
{
    // TODO
    std::uint64_t time_ns()
    {
        return 0;
    }

    lib::initgraph::stage *initialised_stage()
    {
        static lib::initgraph::stage stage
        {
            "timers.arch.generic.initialised",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    time::clock clock { "generic", 0, time_ns };

    lib::initgraph::task generic_task
    {
        "timers.arch.generic.initialise",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { ::timers::arch::can_initialise_stage() },
        lib::initgraph::entail { initialised_stage() },
        [] {
            // if (const auto clock = time::main_clock())
            //     offset = time_ns() - clock->ns();

            // time::register_clock(clock);
            // initialised = true;
        }
    };
} // namespace aarch64::timers::generic