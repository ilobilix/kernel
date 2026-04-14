// Copyright (C) 2024-2026  ilobilo

module arch.drivers.timers;

import drivers.timers;
import arch;
import lib;

namespace timers::arch
{
    using namespace x86_64::timers;

    auto calibrator() -> std::size_t (*)(std::size_t ms)
    {
        if (kvm::supported())
            return kvm::calibrate;
        else if (hpet::is_initialised())
            return hpet::calibrate;
        // else if (pit::is_initialised())
        //     return pit::calibrate;

        return nullptr;
    }

    lib::initgraph::task timers_task
    {
        "timers.arch",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require {
            // rtc::initialised_stage(),
            pit::initialised_stage(),
            hpet::initialised_stage(),
            kvm::initialised_stage(),
            tsc::initialised_stage()
        },
        lib::initgraph::entail { initialised_stage() },
        [] { }
    };
} // expo namespace timers::arch