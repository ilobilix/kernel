// Copyright (C) 2024-2025  ilobilo

module;

#include <uacpi/types.h>

export module drivers.timers.acpipm;
import cppstd;

export namespace timers::acpipm
{
    constexpr std::size_t frequency = 3579545;
    bool initialised = false;

    bool supported();
    // uacpi_interrupt_ret handle_overflow(uacpi_handle);

    // std::uint64_t time_ns();
    void calibrate(std::size_t ms);

    void init();
    // void finalise();
} // export namespace timers::acpipm