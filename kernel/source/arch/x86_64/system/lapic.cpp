// Copyright (C) 2024  ilobilo

module;

#include <uacpi/tables.h>
#include <uacpi/acpi.h>

module x86_64.system.lapic;

import system.memory;
import system.cpu;
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
        } // namespace reg

        std::uintptr_t _pmmio;
        std::uintptr_t _mmio;
        bool _x2apic = false;

        std::uint32_t to_x2apic(std::uint32_t reg)
        {
            return (reg >> 4) + 0x800;
        }

        // std::uint32_t read(std::uint32_t reg)
        // {
        //     if (_x2apic)
        //         return cpu::msr::read(to_x2apic(reg));

        //     return lib::mmio::in<32>(_mmio + reg);
        // }

        void write(std::uint32_t reg, std::uint32_t val)
        {
            if (_x2apic)
                cpu::msr::write(to_x2apic(reg), val);
            else
                lib::mmio::out<32>(_mmio + reg, val);
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
            return true;
        } ();

        return { lapic, !x2apic && cached };
    }

    void eoi() { write(0xB0, 0); }
    void ipi(std::uint8_t id, dest dsh, std::uint8_t vector)
    {
        auto flags = (static_cast<std::uint32_t>(dsh) << 18) | vector;
        if (!_x2apic)
        {
            write(reg::icrh, static_cast<std::uint32_t>(id) << 24);
            write(reg::icrl, flags);
        }
        else write(reg::icrl, (static_cast<std::uint64_t>(id) << 32) | flags);
    }

    void init_cpu()
    {
        auto [lapic, x2apic] = supported();
        if (!lapic)
            lib::panic("CPU does not support lapic");

        auto val = cpu::msr::read(reg::apic_base);
        const bool is_bsp = (val & (1 << 8));
        const auto phys_mmio = val & ~0xFFF;

        if (!is_bsp)
            lib::ensure(_x2apic == x2apic, "x2apic support differs from the BSP");
        else
            _x2apic = x2apic;

        // APIC global enable
        val |= (1 << 11);
        // x2APIC enable
        if (_x2apic)
            val |= (1 << 10);
        cpu::msr::write(reg::apic_base, val);

        if (is_bsp)
            log::debug("lapic: x2apic: {}, physical mmio: 0x{:X}", _x2apic, phys_mmio);

        if (!_x2apic)
        {
            if (is_bsp)
            {
                _pmmio = phys_mmio;
                _mmio = lib::fromhh(vmm::alloc_vpages(vmm::vspace::other, 1));

                log::debug("lapic: mapping mmio to 0x{:X}", _mmio);

                if (!vmm::kernel_pagemap->map(_mmio, _pmmio, pmm::page_size, vmm::flag::rw, vmm::page_size::small, vmm::caching::mmio))
                    lib::panic("could not map lapic mmio");
            }
            else lib::ensure(phys_mmio == _pmmio, "APIC mmio address differs from the BSP");
        }

        // Enable all external interrupts
        write(reg::tpr, 0x00);
        // Enable APIC and set spurious interrupt vector to 0xFF
        write(reg::siv, (1 << 8) | 0xFF);
    }
} // namespace x86_64::apic