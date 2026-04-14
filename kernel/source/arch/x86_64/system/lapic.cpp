// Copyright (C) 2024  ilobilo

module;

#include <uacpi/tables.h>
#include <uacpi/acpi.h>

module x86_64.system.lapic;

import system.memory;
import system.cpu.self;
import system.cpu;
import lib;
import std;

namespace x86_64::apic
{
    namespace reg
    {
        constexpr std::uintptr_t apic_base = 0x1B;
        constexpr std::uintptr_t tpr = 0x80;
        constexpr std::uintptr_t eoi = 0xB0;
        constexpr std::uintptr_t siv = 0xF0;
        constexpr std::uintptr_t icrl = 0x300;
        constexpr std::uintptr_t icrh = 0x310;
    } // namespace reg

    std::uint32_t lapic::read(std::uint32_t reg) const
    {
        if (_x2apic)
            return cpu::msr::read(to_x2apic(reg));

        return lib::mmio::in<32>(_mmio + reg);
    }

    void lapic::write(std::uint32_t reg, std::uint32_t val) const
    {
        if (_x2apic)
            cpu::msr::write(to_x2apic(reg), val);
        else
            lib::mmio::out<32>(_mmio + reg, val);
    }

    std::pair<bool, bool> lapic::supported() const
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
                auto flags = reinterpret_cast<acpi_dmar *>(table.virt_addr)->flags;
                if ((flags & (1 << 0)) && (flags & (1 << 1)))
                    return false;
            }
            return true;
        } ();

        return { lapic, x2apic && cached };
    }

    void lapic::eoi()
    {
        write(reg::eoi, 0);
    }

    void lapic::ipi(std::uint8_t id, dest dsh, std::uint8_t vector)
    {
        auto flags = (static_cast<std::uint32_t>(dsh) << 18) | vector;
        if (!_x2apic)
        {
            write(reg::icrh, static_cast<std::uint32_t>(id) << 24);
            write(reg::icrl, flags);
        }
        else write(reg::icrl, (static_cast<std::uint64_t>(id) << 32) | flags);
    }

    lapic::lapic() : _x2apic { false }
    {
        auto [lapic, x2apic] = supported();
        if (!lapic)
            lib::panic("CPU does not support LAPIC");

        _x2apic = x2apic;

        auto val = cpu::msr::read(reg::apic_base);
        {
            // APIC global enable
            val |= (1 << 11);
            // x2APIC enable
            if (_x2apic)
                val |= (1 << 10);
        }
        cpu::msr::write(reg::apic_base, val);

        const bool is_bsp = (val & (1 << 8));
        const auto phys_mmio = val & ~0xFFF;

        if (is_bsp)
        {
            log::info("Setting up Local APIC");
            log::debug("x2APIC: {}, APIC base address 0x{:X}", _x2apic, phys_mmio);
        }
        else log::debug("Setting up Local APIC");

        if (!_x2apic)
        {
            if (is_bsp)
            {
                _pmmio = phys_mmio;
                _mmio = vmm::alloc_vspace(vmm::vspace::other, pmm::page_size, pmm::page_size);

                log::debug("Mapping APIC base address to 0x{:X}", _mmio);

                if (!vmm::kernel_pagemap->map(_mmio, _pmmio, pmm::page_size, vmm::flag::rw, vmm::page_size::small, vmm::caching::mmio))
                    lib::panic("Could not map APIC base address to 0x{:X}", _mmio);
            }
            else lib::ensure(phys_mmio == _pmmio, "APIC base address differs from the BSP");
        }

        // Enable all external interrupts
        write(reg::tpr, 0x00);
        // Enable APIC and set spurious interrupt vector to 0xFF
        write(reg::siv, (1 << 8) | 0xFF);
    }
} // namespace x86_64::apic