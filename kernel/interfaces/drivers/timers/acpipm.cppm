// Copyright (C) 2024-2025  ilobilo

module;

#include <uacpi/types.h>

export module drivers.timers.acpipm;

import lib;
import cppstd;

export namespace timers::acpipm
{
    bool supported();
    void calibrate(std::size_t ms);

    initgraph::stage *available_stage();
} // export namespace timers::acpipm