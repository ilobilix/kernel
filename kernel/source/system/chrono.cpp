// Copyright (C) 2024-2025  ilobilo

module system.chrono;

import frigg;
import arch;
import boot;
import lib;
import std;

namespace chrono
{
    namespace
    {
        struct higher_priority
        {
            constexpr bool operator()(const clock *lhs, const clock *rhs) const
            {
                return lhs->priority < rhs->priority;
            }
        };

        frg::pairing_heap<
            clock,
            frg::locate_member<
                clock,
                frg::pairing_heap_hook<clock>,
                &clock::hook
            >,
            higher_priority
        > clocks;
        clock *main = nullptr;
    } // namespace

    void register_clock(clock &clock)
    {
        lib::info("time: registering clock source '{}'", clock.name);
        clocks.push(&clock);
        lib::debug("time: main clock is set to '{}'", (main = clocks.top())->name);
    }

    clock *main_clock()
    {
        return main;
    }

    bool stall_ns(std::uint64_t ns)
    {
        if (main == nullptr)
            return false;

        const auto target = main->ns() + ns;
        while (main->ns() < target)
            arch::pause();

        return true;
    }

    // TODO: this is terrible and only temporary
    timespec now(clockid_t clockid)
    {
        lib::unused(clockid);

        if (main == nullptr)
            return timespec { };

        const auto boot_time_s = boot::time();
        const auto clock_ns = main->ns();
        return timespec {
            static_cast<time_t>(boot_time_s + clock_ns / 1'000'000'000),
            static_cast<long>(clock_ns % 1'000'000'000)
        };
    }
} // namespace chrono