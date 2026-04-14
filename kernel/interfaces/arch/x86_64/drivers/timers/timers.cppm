// Copyright (C) 2024-2025  ilobilo

export module arch.drivers.timers;

export import x86_64.drivers.timers.hpet;
export import x86_64.drivers.timers.kvm;
export import x86_64.drivers.timers.pit;
export import x86_64.drivers.timers.rtc;
export import x86_64.drivers.timers.tsc;

import lib;
import cppstd;

export namespace timers::arch
{
    using namespace x86_64::timers;
    auto calibrator() -> std::size_t (*)(std::size_t ms);
} // export namespace timers::arch