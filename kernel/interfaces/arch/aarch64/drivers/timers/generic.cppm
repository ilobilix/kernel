// Copyright (C) 2024-2025  ilobilo

export module aarch64.drivers.timers.generic;

import lib;
import cppstd;

export namespace aarch64::timers::generic
{
    bool is_initialised();
    std::uint64_t time_ns();

    lib::initgraph::stage *initialised_stage();
} // export namespace aarch64::timers::generic