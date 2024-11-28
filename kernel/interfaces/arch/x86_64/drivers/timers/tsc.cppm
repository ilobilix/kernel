// Copyright (C) 2024  ilobilo

export module x86_64.drivers.timers.tsc;

import x86_64.drivers.timers.kvm;
import x86_64.drivers.timers.pit;
import drivers.timers.acpipm;
import system.cpu.self;
import system.cpu;
import system.time;
import lib;
import std;

namespace x86_64::timers::tsc
{
    export bool supported()
    {
        static const auto cached = []
        {
            std::uint32_t a, b, c, d;
            // auto tsc = cpu::id(0x01, 0, a, b, c, d) && (d & (1 << 4));
            auto invariant = cpu::id(0x80000007, 0, a, b, c, d) && (d & (1 << 8));
            log::info("Invariant TSC supported: {}", invariant);
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

    export std::uint64_t time_ns()
    {
        auto &self = cpu::self()->arch.tsc;
        if (!self.calibrated) [[unlikely]]
            lib::panic("TSC not calibrated");

        return lib::ticks2ns(rdtsc(), self.p, self.n) - self.offset;
    }

    time::clock clock { "tsc", 75, time_ns };
    namespace
    {
        template<auto Func>
        void use_timer(std::size_t ms)
        {
            auto end = Func() + (ms * 1'000'000);
            while (Func() < end) { }
        }

        void calibrate(auto &self)
        {
            std::uint64_t freq = 0;

            std::uint32_t a, b, c, d;
            if (cpu::id(0x15, 0, a, b, c, d) && a != 0 && b != 0 && c != 0)
            {
                freq = c * b / a;
                self.tsc.calibrated = true;
            }
            else if (kvm::supported() && self.kvm.pvclock)
            {
                freq = kvm::tsc_freq();
                self.tsc.calibrated = true;
            }
            else
            {
                void (*calibrate)(std::size_t ms) = nullptr;
                if (::timers::acpipm::supported())
                    calibrate = ::timers::acpipm::calibrate;
                else if (pit::initialised)
                    calibrate = use_timer<pit::time_ns>;
                else
                    return;

                static constexpr std::size_t millis = 50;
                static constexpr std::size_t times = 3;

                for (std::size_t i = 0; i < times; i++)
                {
                    auto start = rdtsc();
                    calibrate(millis);
                    auto end = rdtsc();

                    freq += (end - start) * (1'000 / millis);
                }
                freq /= times;

                self.tsc.calibrated = true;
            }

            if (self.tsc.calibrated)
            {
                std::tie(self.tsc.p, self.tsc.n) = lib::freq2nspn(freq);

                if (auto clock = time::main_clock(); clock)
                    self.tsc.offset = time_ns() - clock->ns();

                log::debug("TSC frequency: {} hz", freq);
            }
        }
    } // namespace

    export void init()
    {
        if (!supported())
            return;

        auto &self = cpu::self()->arch;
        calibrate(self);

        if (!self.tsc.calibrated)
            log::debug("TSC not calibrated");
    }

    export void finalise()
    {
        if (supported())
            time::register_clock(clock);
    }
} // export namespace x86_64::timers::tsc