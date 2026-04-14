// Copyright (C) 2024-2025  ilobilo

export module x86_64.drivers.timers.kvm;
import cppstd;

export namespace x86_64::timers::kvm
{
    bool supported();

    std::uint64_t time_ns();
    std::uint64_t tsc_freq();

    void init();
} // export namespace x86_64::timers::kvm