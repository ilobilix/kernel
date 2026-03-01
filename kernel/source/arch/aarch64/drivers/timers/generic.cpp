// Copyright (C) 2024-2026  ilobilo

module aarch64.drivers.timers.generic;

import system.cpu.local;
import system.cpu;
import system.chrono;
import drivers.timers;

import lib;
import std;

namespace aarch64::timers::generic
{
    // namespace
    // {
    //     bool initialised = false;
    //     // TODO
    //     std::uint64_t time_ns()
    //     {
    //         return 0;
    //     }
    // } // namespace

    lib::initgraph::stage *initialised_stage()
    {
        static lib::initgraph::stage stage
        {
            "timers.arch.generic.initialised",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task generic_task
    {
        "timers.arch.generic",
        lib::initgraph::presched_init_engine,
        // lib::initgraph::require { },
        lib::initgraph::entail { initialised_stage() },
        [] {
            // initialised = true;
            // static chrono::clock clock { "generic", 0, time_ns };
            // chrono::register_clock(clock);
        }
    };
} // namespace aarch64::timers::generic