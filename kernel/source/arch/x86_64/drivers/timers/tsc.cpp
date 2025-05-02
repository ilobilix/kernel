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
    cpu_local_init(local);

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

    extern "C++" std::uint64_t rdtsc()
    {
        std::uint32_t a = 0, d = 0;
        asm volatile ("lfence; rdtsc" : "=a"(a), "=d"(d));
        return static_cast<std::uint64_t>(a) | (static_cast<std::uint64_t>(d) << 32);
    }

    std::uint64_t time_ns()
    {
        if (!local->calibrated) [[unlikely]]
            lib::panic("TSC not calibrated");

        return lib::ticks2ns(rdtsc(), local->p, local->n) - local->offset;
    }

    time::clock clock { "tsc", 75, time_ns };
    bool is_calibrated = false;

    namespace
    {
        void calibrate()
        {
            std::uint64_t freq = 0;

            std::uint32_t a, b, c, d;
            if (cpu::id(0x15, 0, a, b, c, d) && a != 0 && b != 0 && c != 0)
            {
                freq = c * b / a;
                local->calibrated = true;
            }
            else if (kvm::supported())
            {
                freq = kvm::tsc_freq();
                local->calibrated = true;
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

                    freq += (end - start) * (1'000 / millis);
                }
                freq /= times;

                local->calibrated = true;
            }

            if (local->calibrated)
            {
                std::tie(local->p, local->n) = lib::freq2nspn(freq);

                if (const auto clock = time::main_clock())
                    local->offset = time_ns() - clock->ns();

                log::debug("tsc: frequency: {} hz", freq);

                is_calibrated = true;
            }
        }
    } // namespace

    void init()
    {
        if (!supported())
            return;

        calibrate();

        if (!local->calibrated)
            log::debug("tsc: not calibrated");
    }

    void finalise()
    {
        if (is_calibrated)
            time::register_clock(clock);
    }
} // namespace x86_64::timers::tsc