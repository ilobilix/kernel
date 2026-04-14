// Copyright (C) 2024-2025  ilobilo

module x86_64.drivers.timers.tsc;

import drivers.timers;
import system.cpu.self;
import system.cpu;
import system.time;

import lib;
import cppstd;

namespace x86_64::timers::tsc
{
    namespace
    {
        lib::freqfrac freq;

        cpu_local<std::int64_t> offset;
        cpu_local_init(offset, 0);

        bool is_calibrated = false;
    } // namespace

    bool supported()
    {
        static const auto cached = []
        {
            std::uint32_t a, b, c, d;
            // auto tsc = cpu::id(0x01, 0, a, b, c, d) && (d & (1 << 4));
            auto invariant = cpu::id(0x80000007, 0, a, b, c, d) && (d & (1 << 8));
            log::info("tsc: is invariant: {}", invariant);
            return invariant;
        } ();
        return cached;
    }

    lib::freqfrac frequency() { return freq; }

    extern "C++" std::uint64_t rdtsc()
    {
        std::uint32_t a = 0, d = 0;
        asm volatile ("lfence; rdtsc" : "=a"(a), "=d"(d));
        return static_cast<std::uint64_t>(a) | (static_cast<std::uint64_t>(d) << 32);
    }

    std::uint64_t time_ns()
    {
        if (!is_calibrated) [[unlikely]]
            lib::panic("tsc not calibrated");

        return freq.nanos(rdtsc()) - offset.get();
    }

    void init()
    {
        if (!supported())
            return;

        if (cpu::self()->idx == cpu::bsp_idx())
        {
            std::uint64_t val = 0;

            std::uint32_t a, b, c, d;
            if (cpu::id(0x15, 0, a, b, c, d) && a != 0 && b != 0 && c != 0)
            {
                val = c * b / a;
            }
            else if (kvm::supported())
            {
                val = kvm::tsc_freq();
            }
            else if (auto calibrator = ::timers::calibrator())
            {
                static constexpr std::size_t millis = 50;
                static constexpr std::size_t times = 3;

                for (std::size_t i = 0; i < times; i++)
                {
                    auto start = rdtsc();
                    calibrator(millis);
                    auto end = rdtsc();

                    val += (end - start) * (1'000 / millis);
                }
                val /= times;
            }

            if (val != 0)
            {
                log::debug("tsc: frequency: {} hz", val);
                freq = val;
                is_calibrated = true;
            }
            else log::debug("tsc: not calibrated");
        }

        if (is_calibrated)
        {
            if (const auto clock = time::main_clock())
                offset = time_ns() - clock->ns();
        }
    }

    time::clock clock { "tsc", 75, time_ns };
    void finalise()
    {
        if (is_calibrated)
            time::register_clock(clock);
    }
} // namespace x86_64::timers::tsc