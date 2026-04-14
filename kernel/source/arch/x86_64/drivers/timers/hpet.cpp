// Copyright (C) 2024-2025  ilobilo

module;

#include <uacpi/tables.h>
#include <uacpi/acpi.h>

module x86_64.drivers.timers.hpet;

import system.memory;
import system.time;
import boot;
import arch;
import lib;
import cppstd;

namespace x86_64::timers::hpet
{
    namespace
    {
        volatile struct [[gnu::packed]]
        {
            std::uint64_t cap;
            std::uint64_t rsvd0;
            std::uint64_t cfg;
            std::uint64_t rsvd1;
            std::uint64_t ist;
            std::uint64_t rsvd2[25];
            union [[gnu::packed]]
            {
                std::uint64_t counter64;
                std::uint64_t counter32;
            };
            std::uint64_t rsvd3;
            struct [[gnu::packed]]
            {
                std::uint64_t cmd;
                std::uint64_t val;
                std::uint64_t fsb;
                std::uint64_t rsvd0;
            } comparators[];
        } *regs;

        std::uintptr_t paddr;
        bool is_64bit;

        std::int64_t offset = 0;
        std::uint64_t p, n;

        // TODO
        std::size_t overflows = 0;
        uint128_t read()
        {
            uint128_t counter = is_64bit ? regs->counter64 : regs->counter32;
            return counter + static_cast<uint128_t>(overflows) * (is_64bit ? -1ul : -1u);
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

    std::uint64_t time_ns()
    {
        lib::ensure(!!initialised);
        return lib::ticks2ns(read(), p, n) - offset;
    }

    void calibrate(std::size_t ms)
    {
        const auto ticks = (ms * frequency) / 1'000;

        const auto start = read();
        auto current = start;

        while (current < start + ticks)
            current = read();
    }

    time::clock clock { "hpet", 125, time_ns };
    void init()
    {
        log::info("hpet: supported: {}", supported());

        const auto vaddr = vmm::alloc_vpages(vmm::space_type::other, 1);
        log::debug("hpet: mapping to 0x{:X}", vaddr);

        const auto psize = vmm::page_size::small;
        const auto npsize = vmm::pagemap::from_page_size(psize);

        if (!vmm::kernel_pagemap->map(vaddr, paddr, npsize, vmm::flag::rw, psize, vmm::caching::mmio))
            lib::panic("could not map HPET");

        regs = reinterpret_cast<decltype(regs)>(vaddr);

        is_64bit = (regs->cap & ACPI_HPET_COUNT_SIZE_CAP);

        frequency = 1'000'000'000'000'000ull / (regs->cap >> 32);
        std::tie(p, n) = lib::freq2nspn(frequency);

        log::debug("hpet: timer is {} bit, frequency: {} hz", is_64bit ? "64" : "32", frequency);

        // enable main counter
        regs->cfg = 1;

        initialised = true;
        if (const auto clock = time::main_clock())
            offset = time_ns() - clock->ns();

        time::register_clock(clock);
    }
} // namespace x86_64::timers::hpet