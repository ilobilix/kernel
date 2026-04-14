// Copyright (C) 2024-2025  ilobilo

module;

#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include <uacpi/io.h>

module drivers.timers.acpipm;

import system.acpi;
import system.time;
import lib;
import std;

namespace timers::acpipm
{
    namespace
    {
        acpi_gas timer_block;
        uacpi_mapped_gas *mapped;
        std::size_t mask;
        std::int64_t offset = 0;

        std::uint64_t read()
        {
            std::uint64_t value;
            uacpi_gas_read_mapped(mapped, &value);
            return value;
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
        static constexpr auto pn = lib::freq2nspn(frequency);

        // lib::ensure(!!initialised);
        return lib::ticks2ns(read() + (overflows * mask), pn.first, pn.second) - offset;
    }

    void calibrate(std::size_t ms)
    {
        lib::ensure(supported() && (ms * frequency) / 1'000 <= mask);

        const auto ticks = (ms * frequency) / 1'000;

        const auto start = read();
        auto current = start;

        while (current < start + ticks)
        {
            current = read();
            if (current < start)
                current += mask;
        }
    }

    void init()
    {
        auto pmtimer = supported();
        log::info("acpipm: timer supported: {}", pmtimer);
    }

    time::clock clock { "acpipm", 50, time_ns };
    void finalise()
    {
        lib::ensure(!!supported());

        initialised = true;

        if (const auto clock = time::main_clock())
            offset = time_ns() - clock->ns();

        time::register_clock(clock);
    }
} // namespace timers::acpipm