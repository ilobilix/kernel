// Copyright (C) 2024-2025  ilobilo

module lib;

import system.time;
import arch;
import cppstd;

namespace lib::lock
{
    bool lock()
    {
        auto ints = arch::int_status();
        arch::int_switch(false);
        return ints;
    }

    void unlock(bool ints)
    {
        if (arch::int_status() != ints)
            arch::int_switch(ints);
    }

    void pause()
    {
        arch::pause();
    }

    // auto clock() -> std::uint64_t (*)();
    std::uint64_t (*clock())()
    {
        auto clock = time::main_clock();
        if (clock == nullptr)
            return nullptr;
        return clock->ns;
    }
} // namespace lib::lock