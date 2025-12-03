// Copyright (C) 2024-2025  ilobilo

export module arch.drivers.timers;

export import aarch64.drivers.timers.generic;

import lib;
import std;

export namespace timers::arch
{
    using namespace aarch64::timers;
    auto calibrator() -> std::size_t (*)(std::size_t ms);
} // export namespace timers::arch