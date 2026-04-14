// Copyright (C) 2024-2025  ilobilo

module;

#include <uacpi/tables.h>
#include <uacpi/acpi.h>

module x86_64.drivers.timers.hpet;

import system.scheduler;
import system.memory;
import system.time;
import drivers.timers;
import magic_enum;
import boot;
import arch;
import lib;
import std;

namespace x86_64::timers::hpet
{
    namespace regs
    {
        constexpr std::size_t cap = 0x00;
        constexpr std::size_t cfg = 0x10;
        constexpr std::size_t cnt = 0xF0;
    } // namespace regs

    namespace
    {
        bool initialised = false;

        std::uintptr_t paddr;
        std::uintptr_t vaddr;
        bool is_64bit;

        std::int64_t offset = 0;
        lib::freqfrac freq;

        std::uint64_t read(std::size_t offset)
        {
            return lib::mmio::in<64>(vaddr + offset);
        }

        void write(std::size_t offset, std::uint64_t value)
        {
            lib::mmio::out<64>(vaddr + offset, value);
        }

        // TODO: 64 bit overflows? :trl:
        std::uint64_t read()
        {
            constexpr std::uint64_t mask = 0xFFFFFFFFul;
            static std::atomic<std::uint64_t> last = 0;

            auto value = read(regs::cnt);
            if (is_64bit)
                return value;

            auto last_val = last.load(std::memory_order_relaxed);
            value &= mask;
            value |= last_val & ~mask;

            if (value < last_val)
                value += (1ul << 32);

            if (last_val - value > (mask >> 1))
                last.compare_exchange_strong(
                    last_val, value,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed
                );

            return value;
        }

        void handle_overflow()
        {
            while (true)
            {
                read();
                sched::sleep_for(1'000);
            }
        }
    } // namespace

    bool supported()
    {
        static const auto cached = [] -> bool
        {
            uacpi_table table;
            if (uacpi_table_find_by_signature(ACPI_HPET_SIGNATURE, &table) != UACPI_STATUS_OK)
                return false;

            const auto hpet = static_cast<acpi_hpet *>(table.ptr);
            if (hpet->address.address_space_id != UACPI_ADDRESS_SPACE_SYSTEM_MEMORY)
            {
                uacpi_table_unref(&table);
                return false;
            }
            paddr = hpet->address.address;

            uacpi_table_unref(&table);
            return true;
        } ();
        return cached;
    }

    bool is_initialised() { return initialised; }

    std::uint64_t time_ns()
    {
        lib::bug_on(!initialised);
        return freq.nanos(read()) - offset;
    }

    std::size_t calibrate(std::size_t ms)
    {
        const auto ticks = freq.ticks(ms * 1'000'000);

        const auto start = read();
        auto current = start;

        while (current < start + ticks)
            current = read();

        return freq.nanos(current - start);
    }

    lib::initgraph::stage *initialised_stage()
    {
        static lib::initgraph::stage stage
        {
            "timers.arch.hpet.initialised",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    time::clock clock { "hpet", 50, time_ns };

    lib::initgraph::task hpet_task
    {
        "timers.arch.hpet.initialise",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { ::timers::arch::can_initialise_stage() },
        lib::initgraph::entail { initialised_stage() },
        [] {
            log::info("hpet: supported: {}", supported());

            vaddr = vmm::alloc_vspace(1);
            log::debug("hpet: mapping mmio: 0x{:X} -> 0x{:X}", paddr, vaddr);

            const auto psize = vmm::page_size::small;
            const auto npsize = vmm::pagemap::from_page_size(psize);
            const auto flags = vmm::pflag::rwg;
            const auto caching = vmm::caching::mmio;

            if (const auto ret = vmm::kernel_pagemap->map(vaddr, paddr, npsize, flags, psize, caching); !ret)
                lib::panic("could not map hpet: {}", magic_enum::enum_name(ret.error()));

            const auto cap = read(regs::cap);
            is_64bit = (cap & ACPI_HPET_COUNT_SIZE_CAP);

            freq = 1'000'000'000'000'000ull / (cap >> 32);

            log::debug("hpet: timer is {} bit, frequency: {} hz", is_64bit ? "64" : "32", freq.frequency());

            // enable main counter
            write(regs::cfg, 1);

            initialised = true;
            if (const auto clock = time::main_clock())
                offset = time_ns() - clock->ns();
            else
                offset = time_ns();

            time::register_clock(clock);
        }
    };

    lib::initgraph::task hpet_thread_task
    {
        "timers.arch.hpet.create-thread",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { ::timers::arch::initialised_stage(), sched::pid0_initialised_stage() },
        [] {
            if (!is_64bit)
                sched::spawn(0, reinterpret_cast<std::uintptr_t>(handle_overflow));
        }
    };
} // namespace x86_64::timers::hpet