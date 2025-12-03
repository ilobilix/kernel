// Copyright (C) 2024-2025  ilobilo

module;

#include <uacpi/types.h>

export module drivers.timers.acpipm;

import lib;
import std;

export namespace timers::acpipm
{
    bool supported();
    std::uint64_t time_ns();
    std::size_t calibrate(std::size_t ms);

    lib::initgraph::stage *initialised_stage();
} // export namespace timers::acpipm