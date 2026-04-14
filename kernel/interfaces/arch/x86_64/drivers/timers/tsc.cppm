// Copyright (C) 2024-2025  ilobilo

export module x86_64.drivers.timers.tsc;
import system.cpu.self;
import cppstd;

export namespace x86_64::timers::tsc
{
    struct tsc_local
    {
        std::uint64_t n, p;
        std::int64_t offset = 0;
        bool calibrated = false;
    };
    cpu_local<tsc_local> local;

    bool supported();

    extern "C++" std::uint64_t rdtsc();
    std::uint64_t time_ns();

    void init();
    void finalise();
} // export namespace x86_64::timers::tsc