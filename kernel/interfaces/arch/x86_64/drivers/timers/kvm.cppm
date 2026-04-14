// Copyright (C) 2024-2026  ilobilo

export module x86_64.drivers.timers.kvm;

import lib;
import std;

export namespace x86_64::timers::kvm
{
    bool supported();

    std::uint64_t tsc_freq();
    std::size_t calibrate(std::size_t ms);

    void init_cpu();

    lib::initgraph::stage *initialised_stage();
} // export namespace x86_64::timers::kvm