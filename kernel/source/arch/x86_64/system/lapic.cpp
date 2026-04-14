// Copyright (C) 2024-2025  ilobilo

module;

#include <uacpi/tables.h>
#include <uacpi/acpi.h>

module x86_64.system.lapic;

import drivers.timers;
import system.memory;
import system.cpu;
import system.cpu.self;
import lib;
import std;

namespace x86_64::apic
{
    namespace
    {
        namespace reg
        {
            constexpr std::uintptr_t apic_base = 0x1B;
            constexpr std::uintptr_t tpr = 0x80;
            constexpr std::uintptr_t siv = 0xF0;
            constexpr std::uintptr_t icrl = 0x300;
            constexpr std::uintptr_t icrh = 0x310;
            constexpr std::uintptr_t lvt = 0x320;
            constexpr std::uintptr_t tdc = 0x3E0;
            constexpr std::uintptr_t tic = 0x380;
            constexpr std::uintptr_t tcc = 0x390;

            constexpr std::uintptr_t deadline = 0x6E0;
        } // namespace reg

        std::uintptr_t pmmio;
        std::uintptr_t mmio;

        bool x2apic = false;
        bool tsc_deadline = false;

        std::uint32_t to_x2apic(std::uint32_t reg)
        {
            return (reg >> 4) + 0x800;
        }

        std::uint32_t read(std::uint32_t reg)
        {
            if (x2apic)
                return cpu::msr::read(to_x2apic(reg));

            return lib::mmio::in<32>(mmio + reg);
        }

        void write(std::uint32_t reg, std::uint32_t val)
        {
            if (x2apic)
                cpu::msr::write(to_x2apic(reg), val);
            else
                lib::mmio::out<32>(mmio + reg, val);
        }

        bool calibrate_timer()
        {
            if (tsc_deadline)
            {
                log::debug("lapic: using tsc deadline");
                return true;
            }

            auto self = cpu::self();
            std::uint64_t freq = 0;

            std::uint32_t a, b, c, d;
            if (cpu::id(0x15, 0, a, b, c, d) && c != 0)
            {
                freq = c;
                self->arch.lapic.calibrated = true;
            }
            else
            {
                auto calibrator = ::timers::calibrator();
                if (!calibrator)
                    return false;

                write(reg::tdc, 0b1011);

                static constexpr std::size_t millis = 10;
                static constexpr std::size_t times = 3;

                for (std::size_t i = 0; i < times; i++)
                {
                    write(reg::tic, 0xFFFFFFFF);
                    calibrator(millis);
                    auto count = read(reg::tcc);
                    write(reg::tic, 0);

                    freq += (0xFFFFFFFF - count) * 100;
                }
                freq /= times;
                self->arch.lapic.calibrated = true;
            }
            log::debug("lapic: timer frequency: {} hz", freq);

            auto &n = self->arch.lapic.n;
            auto &p = self->arch.lapic.p;
            std::tie(p, n) = lib::freq2nspn(freq);

            return true;
        }
    } // namespace

    std::pair<bool, bool> supported()
    {
        std::uint32_t a, b, c, d;
        const bool cpuid = cpu::id(1, 0, a, b, c, d);
        const bool lapic = cpuid && (d & (1 << 9));
        const bool x2apic = cpuid && (c & (1 << 21));

        static const auto cached = []
        {
            struct [[gnu::packed]] acpi_dmar
            {
                acpi_sdt_hdr hdr;
                std::uint8_t host_address_width;
                std::uint8_t flags;
                std::uint8_t reserved[10];
                char remapping_structures[];
            };

            uacpi_table table;
            if (uacpi_table_find_by_signature("DMAR", &table) == UACPI_STATUS_OK)
            {
                auto flags = static_cast<acpi_dmar *>(table.ptr)->flags;
                uacpi_table_unref(&table);
                if ((flags & (1 << 0)) && (flags & (1 << 1)))
                    return false;
            }

            auto tsc_supported = timers::tsc::supported();
            std::uint32_t a, b, c, d;
            tsc_deadline = tsc_supported && cpu::id(0x01, 0, a, b, c, d) && (c & (1 << 24));
            log::debug("lapic: tsc deadline supported: {}", tsc_deadline);

            return true;
        } ();

        return { lapic, x2apic && cached };
    }

    void eoi() { write(0xB0, 0); }
    void ipi(std::uint8_t id, dest dsh, std::uint8_t vector)
    {
        auto flags = (static_cast<std::uint32_t>(dsh) << 18) | vector;
        if (!x2apic)
        {
            write(reg::icrh, static_cast<std::uint32_t>(id) << 24);
            write(reg::icrl, flags);
        }
        else write(reg::icrl, (static_cast<std::uint64_t>(id) << 32) | flags);
    }

    void arm(std::size_t ns, std::uint8_t vector)
    {
        auto self = cpu::self();
        if (tsc_deadline)
        {
            auto val = timers::tsc::rdtsc();
            auto ticks = lib::ns2ticks(ns, self->arch.tsc.p, self->arch.tsc.n);
            write(reg::lvt, (0b10 << 17) | vector);
            asm volatile ("mfence" ::: "memory");
            cpu::msr::write(reg::deadline, val + ticks);
        }
        else
        {
            write(reg::tic, 0);
            write(reg::lvt, vector);
            auto ticks = lib::ns2ticks(ns, self->arch.lapic.p, self->arch.lapic.n);
            if (ticks == 0)
                ticks = 1;
            write(reg::tic, ticks);
        }
    }

    void init_cpu()
    {
        auto [lapic, _x2apic] = supported();
        if (!lapic)
            lib::panic("CPU does not support lapic");

        auto val = cpu::msr::read(reg::apic_base);
        const bool is_bsp = (val & (1 << 8));
        const auto phys_mmio = val & ~0xFFF;

        if (!is_bsp)
            lib::ensure(x2apic == _x2apic, "x2apic support differs from the BSP");
        else
            x2apic = _x2apic;

        if (is_bsp)
            log::debug("lapic: x2apic supported: {}", x2apic);

        // APIC global enable
        val |= (1 << 11);
        // x2APIC enable
        if (x2apic)
            val |= (1 << 10);
        cpu::msr::write(reg::apic_base, val);

        if (!x2apic)
        {
            if (is_bsp)
            {
                pmmio = phys_mmio;
                mmio = vmm::alloc_vpages(vmm::vspace::other, 1);

                log::debug("lapic: mapping mmio: 0x{:X} -> 0x{:X}", phys_mmio, mmio);

                if (!vmm::kernel_pagemap->map(mmio, pmmio, pmm::page_size, vmm::flag::rw, vmm::page_size::small, vmm::caching::mmio))
                    lib::panic("could not map lapic mmio");
            }
            else lib::ensure(phys_mmio == pmmio, "APIC mmio address differs from the BSP");
        }

        // Enable all external interrupts
        write(reg::tpr, 0x00);
        // Enable APIC and set spurious interrupt vector to 0xFF
        write(reg::siv, (1 << 8) | 0xFF);

        if (!calibrate_timer())
            lib::panic("lapic: could not calibrate timer");
    }
} // namespace x86_64::apic