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

    export
    {
        constexpr std::size_t frequency = 3579545;
        bool initialised = false;
        std::atomic_size_t overflows = 0;

        bool supported()
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

        std::uint64_t time_ns()
        {
            static constexpr auto pn = lib::freq2nspn(frequency);

            lib::ensure(!!initialised);
            return lib::ticks2ns(read() + (overflows * mask), pn.first, pn.second) - offset;
        }

        void calibrate(std::size_t ms)
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

        void init()
        {
            auto pmtimer = supported();
            log::info("ACPI PM timer supported: {}", pmtimer);
        }

        time::clock clock { "acpipm", 50, time_ns };
        void finalise()
        {
            lib::ensure(!!supported());

            initialised = true;
            if (auto clock = time::main_clock(); clock)
                offset = time_ns() - clock->ns();

            // TODO: broken
            // time::register_clock(clock);
            initialised = false;
        }
    } // export
} // namespace timers::acpipm