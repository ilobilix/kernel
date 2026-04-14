// Copyright (C) 2024-2026  ilobilo

module;

#include <uacpi/tables.h>
#include <uacpi/acpi.h>

module x86_64.system.lapic;

import drivers.timers;
import system.interrupts;
import system.memory;
import system.cpu.local;
import system.cpu;
import magic_enum;
import lib;
import std;

namespace x86_64::apic
{
    namespace
    {
        std::uintptr_t pmmio;
        std::uintptr_t mmio;

        bool x2apic = false;
        bool tsc_deadline = false;

        lib::freqfrac freq;
        bool is_calibrated = false;

        bool initialised = false;

        std::uint32_t to_x2apic(std::uint32_t reg)
        {
            return (reg >> 4) + 0x800;
        }

        void enable(std::uint64_t val)
        {
            if (!x2apic && (val & (1 << 10)))
            {
                cpu::msr::write(reg::apic_base, val & ~((1 << 11) | (1 << 10)));
                val = cpu::msr::read(reg::apic_base);
            }

            val |= (1 << 11);
            if (x2apic)
                val |= (1 << 10);
            else
                val &= ~(1 << 10);

            cpu::msr::write(reg::apic_base, val);
        }
    } // namespace

    std::pair<bool, bool> supported()
    {
        static cpu::id_res res;
        static const bool cpuid = cpu::id(1, 0, res);
        static const bool lapic = cpuid && (res.d & (1 << 9));
        static const bool x2apic = cpuid && (res.c & (1 << 21));

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
                const auto flags = static_cast<acpi_dmar *>(table.ptr)->flags;
                uacpi_table_unref(&table);
                if ((flags & (1 << 0)) && (flags & (1 << 1)))
                    return false;
            }

            // const auto [hv, _] = cpu::in_hypervisor();
            // if (hv == cpu::hypervisor::none || hv == cpu::hypervisor::kvm)
            // {
                const auto tsc_supported = timers::tsc::supported();
                cpu::id_res res;
                tsc_deadline = tsc_supported && cpu::id(0x01, 0, res) && (res.c & (1 << 24));
            // }
            lib::debug("lapic: tsc deadline supported: {}", tsc_deadline);
            return true;
        } ();

        return { lapic, x2apic && cached };
    }

    bool is_initialised() { return initialised; }

    std::uint32_t read(std::uint32_t reg)
    {
        if (x2apic)
            return cpu::msr::read(to_x2apic(reg));

        return lib::mmio::in<32>(mmio + reg);
    }

    void write(std::uint32_t reg, std::uint64_t val)
    {
        if (!x2apic)
        {
            if (reg == reg::icr)
            {
                asm volatile ("" ::: "memory");
                lib::mmio::out<32>(mmio + reg::icrh, val >> 32);
            }
            lib::mmio::out<32>(mmio + reg, val);
        }
        else
        {
            if (reg == reg::icr)
                asm volatile ("mfence; lfence" ::: "memory");
            cpu::msr::write(to_x2apic(reg), val);
        }
    }

    void calibrate_timer()
    {
        lib::bug_on(!supported().first);

        if (cpu::self()->idx != cpu::bsp_idx())
            return;

        if (tsc_deadline)
        {
            lib::debug("lapic: using tsc deadline");
            is_calibrated = true;
            return;
        }

        std::uint64_t val = 0;

        // if (const auto res = cpu::id(0x15, 0); res && res->a != 0 && res->b != 0 && res->c != 0) { val = res->c; }
        // else if (const auto res = cpu::id(0x16, 0); res && res->a != 0 && res->b != 0 && res->c != 0) { val = res->c; }
        // else
        {
            const auto calibrator = ::timers::calibrator();
            if (!calibrator)
                lib::panic("lapic: could not calibrate timer");

            write(reg::tdc, 0b1011);

            static constexpr std::size_t millis = 10;
            static constexpr std::size_t times = 3;

            for (std::size_t i = 0; i < times; i++)
            {
                write(reg::tic, 0xFFFFFFFF);
                const auto slept_for = calibrator(millis);
                const auto count = read(reg::tcc);
                write(reg::tic, 0);

                val += ((0xFFFFFFFFul - count) * 1'000'000'000ul) / slept_for;
            }
            val /= times;
        }

        lib::debug("lapic: timer frequency: {} hz", val);
        freq = val;
        is_calibrated = true;
    }

    void eoi() { write(0xB0, 0); }

    // ! TODO: xapic sipi doesn't work

    void ipi(shorthand dest, delivery del, std::uint8_t vec)
    {
        const auto val =
            static_cast<std::uint64_t>(del) << 8 |
            static_cast<std::uint64_t>(dest) << 18 |
            (1 << 14) | vec;

        write(reg::icr, val);
    }

    void ipi(std::uint32_t id, destination dest, delivery del, std::uint8_t vec)
    {
        auto val =
            static_cast<std::uint64_t>(del) << 8 |
            static_cast<std::uint64_t>(dest) << 10 |
            (1 << 14) | vec;

        if (x2apic)
            val |= static_cast<std::uint64_t>(id) << 32;
        else
            val |= static_cast<std::uint64_t>(id & 0xFF) << 56;

        write(reg::icr, val);
    }

    void arm(std::size_t ns, std::uint8_t vector)
    {
        lib::bug_on(!is_calibrated);

        if (ns == 0)
            ns = 1;

        if (tsc_deadline)
        {
            const auto val = timers::tsc::rdtsc();
            const auto ticks = timers::tsc::frequency().ticks(ns);
            write(reg::lvt, (0b10 << 17) | vector);
            asm volatile ("mfence" ::: "memory");
            cpu::msr::write(reg::deadline, val + ticks);
        }
        else
        {
            write(reg::tic, 0);
            write(reg::lvt, vector);
            const auto ticks = freq.ticks(ns);
            write(reg::tic, ticks);
        }
    }

    void init_bsp()
    {
        auto [lapic, _x2apic] = supported();
        if (!lapic)
            lib::panic("CPU does not support lapic");

        x2apic = _x2apic;
        lib::debug("lapic: x2apic supported: {}", x2apic);

        auto val = cpu::msr::read(reg::apic_base);
        enable(val);

        if (!x2apic)
        {
            pmmio = val & 0xFFFFF000;
            mmio = vmm::alloc_vspace(1);

            lib::debug("lapic: mapping mmio: 0x{:X} -> 0x{:X}", pmmio, mmio);

            const auto psize = vmm::page_size::small;
            const auto npsize = vmm::pagemap::from_page_size(psize);
            const auto flags = vmm::pflag::rwg;
            const auto cache = vmm::caching::mmio;

            if (const auto ret = vmm::kernel_pagemap->map(mmio, pmmio, npsize, flags, psize, cache); !ret)
                lib::panic("could not map lapic mmio: {}", magic_enum::enum_name(ret.error()));
        }
    }

    void init_cpu()
    {
        if (cpu::self()->idx != cpu::bsp_idx())
        {
            auto val = cpu::msr::read(reg::apic_base);
            if (!x2apic && pmmio != (val & 0xFFFFF000))
            {
                val &= ~0xFFFFF000;
                val |= pmmio;
            }
            enable(val);
        }

        // Enable all external interrupts
        write(reg::tpr, 0x00);

        auto ret = interrupts::allocate(cpu::self()->idx, 0xFF);
        lib::bug_on(!ret.has_value() || ret->second != 0xFF);

        // Enable APIC and set spurious interrupt vector to 0xFF
        write(reg::siv, (1 << 8) | 0xFF);

        // nmi
        write(reg::lint1, (1 << 10));
        lib::io::out<8>(0x70, lib::io::in<8>(0x70) & 0x7F);

        initialised = true;
    }
} // namespace x86_64::apic
