// Copyright (C) 2024-2025  ilobilo

export module system.chrono;

import lib;
import frigg;
import std;

export namespace chrono
{
    struct clock
    {
        frg::pairing_heap_hook<clock> hook;

        std::string name;
        std::size_t priority;

        std::uint64_t (*ns)();

        clock(std::string_view name, std::size_t priority, std::uint64_t (*time_ns)())
            : name { name }, priority { priority }, ns { time_ns } { }
    };

    void register_clock(clock &timer);
    clock *main_clock();

    bool stall_ns(std::size_t ns);

    timespec now(clockid_t clockid = 0);
} // export namespace chrono