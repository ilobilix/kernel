// Copyright (C) 2024-2025  ilobilo

export module aarch64.drivers.timers.generic;
import cppstd;

export namespace aarch64::timers::generic
{
    std::uint64_t time_ns();
    void init();
} // export namespace aarch64::timers::generic