// Copyright (C) 2024-2025  ilobilo

module x86_64.drivers.timers.kvm;

import drivers.timers.acpipm;
import system.cpu.self;
import system.time;
import system.cpu;
import arch;
import lib;
import cppstd;

namespace x86_64::timers::tsc
{
    extern "C++" std::uint64_t rdtsc();
} // x86_64::timers::tsc

namespace x86_64::timers::kvm
{
    namespace
    {
        struct [[gnu::packed]] kvmclock_info
        {
            std::uint32_t version;
            std::uint32_t pad0;
            std::uint64_t tsc_timestamp;
            std::uint64_t system_time;
            std::uint32_t tsc_to_system_mul;
            std::int8_t tsc_shift;
            std::uint8_t flags;
            std::uint8_t pad[2];
        };
        std::int64_t offset = 0;
    } // namespace

    cpu_local<void *> clockptr;
    cpu_local_init(clockptr, nullptr);

    bool supported()
    {
        static const auto cached = [] -> bool
        {
            bool kvmclock = false;
            if (const auto base = cpu::kvm_base())
            {
                cpu::id_res res;
                kvmclock = cpu::id(base + 1, 0, res) && (res.a & (1 << 3));
            }
            log::info("kvmclock: supported: {}", kvmclock);
            return kvmclock;
        } ();
        return cached;
    }

    std::uint64_t time_ns()
    {
        volatile auto pvclock = static_cast<kvmclock_info *>(clockptr.get());

        while (pvclock->version % 2)
            arch::pause();

        auto time = static_cast<uint128_t>(tsc::rdtsc()) - pvclock->tsc_timestamp;
        if (pvclock->tsc_shift >= 0)
            time <<= pvclock->tsc_shift;
        else
            time >>= -pvclock->tsc_shift;
        time = (time * pvclock->tsc_to_system_mul) >> 32;
        time = time + pvclock->system_time;

        return time - offset;
    }

    std::uint64_t tsc_freq()
    {
        volatile auto pvclock = static_cast<kvmclock_info *>(clockptr.get());

        std::uint64_t freq = (1'000'000'000ull << 32) / pvclock->tsc_to_system_mul;
        if (pvclock->tsc_shift < 0)
            freq <<= -pvclock->tsc_shift;
        else
            freq >>= pvclock->tsc_shift;
        return freq;
    }

    time::clock clock { "kvm", 100, time_ns };
    void init()
    {
        if (!supported())
            return;

        clockptr = reinterpret_cast<void *>(new kvmclock_info);
        cpu::msr::write(0x4B564D01, reinterpret_cast<std::uint64_t>(lib::fromhh(clockptr.get())) | 1);

        [[maybe_unused]]
        static const auto cached = []
        {
            if (const auto clock = time::main_clock())
                offset = time_ns() - clock->ns();

            time::register_clock(clock);
            return true;
        } ();
    }
} // namespace x86_64::timers::kvm