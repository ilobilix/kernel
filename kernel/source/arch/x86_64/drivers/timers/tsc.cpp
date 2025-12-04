// Copyright (C) 2024-2025  ilobilo

module x86_64.drivers.timers.tsc;

import x86_64.drivers.timers.kvm;
import drivers.timers;
import system.cpu.local;
import system.cpu;
import system.chrono;

import lib;
import std;

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
            cpu::id_res res;
            const auto invariant = cpu::id(0x80000007, 0, res) && (res.d & (1 << 8));
            lib::info("tsc: is invariant: {}", invariant);
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

    void init_cpu()
    {
        if (!supported())
            return;

        if (cpu::self()->idx == cpu::bsp_idx())
        {
            std::uint64_t val = 0;

            if (const auto res = cpu::id(0x15, 0); res && res->a != 0 && res->b != 0 && res->c != 0)
            {
                val = res->c * res->b / res->a;
            }
            else if (kvm::supported())
            {
                val = kvm::tsc_freq();
            }
            else if (const auto calibrator = ::timers::calibrator())
            {
                static constexpr std::size_t millis = 50;
                static constexpr std::size_t times = 3;

                for (std::size_t i = 0; i < times; i++)
                {
                    const auto start = rdtsc();
                    const auto slept_for = calibrator(millis);
                    const auto end = rdtsc();

                    val += ((end - start) * 1'000'000'000) / slept_for;
                }
                val /= times;
            }

            if (val != 0)
            {
                lib::debug("tsc: frequency: {} hz", val);
                freq = val;
                is_calibrated = true;
            }
            else lib::debug("tsc: not calibrated");
        }

        if (is_calibrated)
        {
            auto &ref = offset.get();
            if (const auto clock = chrono::main_clock())
                ref += time_ns() - clock->ns();
            else
                ref = time_ns();
        }
    }

    lib::initgraph::stage *initialised_stage()
    {
        static lib::initgraph::stage stage
        {
            "timers.arch.tsc.initialised",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task tsc_task
    {
        "timers.arch.tsc.initialise",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { ::timers::arch::can_initialise_stage(), kvm::initialised_stage() },
        lib::initgraph::entail { initialised_stage() },
        [] {
            init_cpu();
        }
    };

    chrono::clock clock { "tsc", 75, time_ns };
    void finalise()
    {
        if (is_calibrated)
            chrono::register_clock(clock);
    }
} // namespace x86_64::timers::tsc