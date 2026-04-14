// Copyright (C) 2024  ilobilo

module;

#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include <uacpi/io.h>

export module drivers.timers.acpipm;

import system.time;
import lib;
import std;

namespace timers::acpipm
{
    export inline constexpr std::size_t frequency = 3579545;
    export bool initialised = false;
    export std::atomic_size_t overflows = 0;

    namespace
    {
        acpi_gas timer_block;
        std::size_t mask;
        std::int64_t offset = 0;

        std::uint64_t read()
        {
            std::uint64_t value;
            uacpi_gas_read(&timer_block, &value);
            return value;
        }
    } // namespace

    export bool supported()
    {
        static const auto cached = [] -> bool
        {
            acpi_fadt *fadt;
            if (uacpi_table_fadt(&fadt) != UACPI_STATUS_OK || fadt == nullptr)
                return false;

            if (fadt->pm_tmr_len != 4)
                return false;

            timer_block = fadt->x_pm_tmr_blk;
            mask = (fadt->flags & (1 << 8)) ? 0xFFFFFFFF : 0xFFFFFF;

            return true;
        } ();
        return cached;
    }

    export std::uint64_t time_ns()
    {
        // lib::ensure(!!initialised);
        auto value = read() + (overflows * mask);
        return (((value * 1'000'000'000) / frequency) & mask) - offset;
    }

    export void calibrate(std::size_t ms)
    {
        // lib::ensure(supported() && (ms * frequency) / 1'000 <= mask);

        auto ticks = (ms * frequency) / 1'000;

        auto start = read();
        auto current = start;

        while (current < start + ticks)
        {
            auto val = read();
            if (val < start)
                val = mask + val;
            current = val;
        }
    }

    export void init()
    {
        auto pmtimer = supported();
        log::info("ACPI PM timer supported: {}", pmtimer);
    }

    time::clock timer { "acpipm", 50, time_ns };
    export void finalise()
    {
        lib::ensure(!!supported());
        // TODO: SCIs are not being triggered so no point registering it for now
        time::register_clock(timer);
        initialised = true;

        if (auto clock = time::main_clock(); clock)
            offset = time_ns() - clock->ns();
    }
} // export namespace timers::acpipm