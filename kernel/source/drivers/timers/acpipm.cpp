// Copyright (C) 2024-2025  ilobilo

module;

#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include <uacpi/io.h>

module drivers.timers.acpipm;

import system.scheduler;
import system.acpi;
import system.time;
import lib;
import std;

namespace timers::acpipm
{
    namespace
    {
        constexpr lib::freqfrac freq { 3579545 };

        acpi_gas timer_block;
        uacpi_mapped_gas *mapped;
        std::size_t mask;

        std::int64_t offset = 0;

        std::uint64_t read(bool fast = false)
        {
            const auto read_internal = [] {
                std::uint64_t value;
                uacpi_gas_read_mapped(mapped, &value);
                return value;
            };

            if (!fast)
            {
                std::uint32_t v1 = 0, v2 = 0, v3 = 0;
                do {
                    v1 = read_internal();
                    v2 = read_internal();
                    v3 = read_internal();
                } while (__builtin_expect(((v1 > v2 && v1 < v3) || (v2 > v3 && v2 < v1) || (v3 > v1 && v3 < v2)), 0));

                return v2;
            }

            return read_internal();
        }

        void handle_overflow()
        {
            while (true)
            {
                time_ns();
                sched::sleep_for(1'000);
            }
        }
    } // namespace

    bool supported()
    {
        static const auto cached = [] -> bool
        {
            if (acpi::fadt == nullptr)
                return false;

            if (acpi::fadt->pm_tmr_len != 4)
                return false;

            timer_block = acpi::fadt->x_pm_tmr_blk;
            uacpi_map_gas(&timer_block, &mapped);

            mask = (acpi::fadt->flags & (1 << 8)) ? 0xFFFFFFFF : 0xFFFFFF;

            return true;
        } ();
        return cached;
    }

    std::uint64_t time_ns()
    {
        lib::bug_on(!supported());

        static std::atomic<std::uint64_t> last = 0;

        auto value = read();

        auto last_val = last.load(std::memory_order_relaxed);
        value &= mask;
        value |= last_val & ~mask;

        if (value < last_val)
            value += mask + 1;

        if (last_val - value > (mask >> 1))
            last.compare_exchange_strong(
                last_val, value,
                std::memory_order_relaxed,
                std::memory_order_relaxed
            );

        return freq.nanos(value) - offset;
    }

    std::size_t calibrate(std::size_t ms)
    {
        lib::bug_on(!supported());

        const auto ticks = freq.ticks(ms * 1'000'000);
        lib::bug_on(ticks > mask);

        const auto start = read(true);
        auto current = start;

        while (current < start + ticks)
        {
            current = read(true);
            if (current < start)
                current += mask + 1;
        }
        return freq.nanos(current - start);
    }

    lib::initgraph::stage *initialised_stage()
    {
        static lib::initgraph::stage stage
        {
            "timers.acpipm.initialised",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    time::clock clock { "acpipm", 25, time_ns };
    lib::initgraph::task acpipm_task
    {
        "timers.acpipm.initialise",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { acpi::tables_stage() },
        lib::initgraph::entail { initialised_stage() },
        [] {
            auto pmtimer = supported();
            log::info("acpipm: timer supported: {}", pmtimer);

            if (const auto clock = time::main_clock())
                offset = time_ns() - clock->ns();
            else
                offset = time_ns();

            time::register_clock(clock);
        }
    };

    lib::initgraph::task acpipm_thread_task
    {
        "timers.acpipm.create-thread",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { sched::pid0_initialised_stage() },
        [] {
            sched::spawn(0, reinterpret_cast<std::uintptr_t>(handle_overflow));
        }
    };
} // namespace timers::acpipm