// Copyright (C) 2024-2025  ilobilo

module;

#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include <uacpi/io.h>

module drivers.timers.acpipm;

import system.acpi;
import system.time;
import lib;
import cppstd;

namespace timers::acpipm
{
    namespace
    {
        constexpr lib::freqfrac freq { 3579545 };

        acpi_gas timer_block;
        uacpi_mapped_gas *mapped;
        std::size_t mask;

        std::uint64_t read()
        {
            const auto read_internal = [] {
                std::uint64_t value;
                uacpi_gas_read_mapped(mapped, &value);
                return value;
            };

            // std::uint32_t v1 = 0, v2 = 0, v3 = 0;
            // do {
            //     v1 = read_internal();
            //     v2 = read_internal();
            //     v3 = read_internal();
            // } while (__builtin_expect(((v1 > v2 && v1 < v3) || (v2 > v3 && v2 < v1) || (v3 > v1 && v3 < v2)), 0));

            return read_internal();
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

    std::size_t calibrate(std::size_t ms)
    {
        lib::bug_on(!supported());

        const auto ticks = freq.ticks(ms * 1'000'000);
        lib::bug_on(ticks > mask);

        const auto start = read();
        auto current = start;

        while (current < start + ticks)
        {
            current = read();
            if (current < start)
                current += mask;
        }
        return freq.nanos(current - start);
    }

    initgraph::stage *available_stage()
    {
        static initgraph::stage stage { "timers.acpipm-available" };
        return &stage;
    }

    initgraph::task acpipm_task
    {
        "timers.init-acpipm",
        initgraph::require { acpi::tables_stage() },
        initgraph::entail { available_stage() },
        [] {
            auto pmtimer = supported();
            log::info("acpipm: timer supported: {}", pmtimer);
        }
    };
} // namespace timers::acpipm