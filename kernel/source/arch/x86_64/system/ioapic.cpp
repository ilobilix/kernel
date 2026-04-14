// Copyright (C) 2024-2025  ilobilo

module;

#include <uacpi/acpi.h>

module x86_64.system.ioapic;

import x86_64.system.pic;
import x86_64.system.idt;
import system.memory;
import system.acpi;
import system.cpu;
import magic_enum;
import lib;
import std;

namespace x86_64::apic::io
{
    namespace
    {
        class ioapic
        {
            private:
            std::uintptr_t _mmio;
            std::uint32_t _gsi_base;
            std::size_t _redirs;

            static constexpr std::uint32_t entry(std::uint32_t idx)
            {
                return 0x10 + (idx * 2);
            }

            std::uint32_t read(std::uint32_t reg) const
            {
                lib::mmio::out<32>(_mmio, reg);
                return lib::mmio::in<32>(_mmio + 0x10);
            }

            void write(std::uint32_t reg, std::uint32_t value) const
            {
                lib::mmio::out<32>(_mmio, reg);
                lib::mmio::out<32>(_mmio + 0x10, value);
            }

            std::uint64_t read_entry(std::uint32_t idx) const
            {
                const auto lo = read(entry(idx));
                const auto hi = read(entry(idx) + 1);
                return (static_cast<std::uint64_t>(hi) << 32) | lo;
            }

            void write_entry(std::uint32_t idx, std::uint64_t value) const
            {
                write(entry(idx), value & 0xFFFFFFFF);
                write(entry(idx) + 1, value >> 32);
            }

            public:
            ioapic(std::uintptr_t mmio, std::uint32_t gsi_base) : _gsi_base { gsi_base }
            {
                _mmio = vmm::alloc_vspace(1);

                log::debug("ioapic: mapping mmio: 0x{:X} -> 0x{:X}", mmio, _mmio);

                const auto psize = vmm::page_size::small;
                const auto npsize = vmm::pagemap::from_page_size(psize);
                const auto flags = vmm::pflag::rwg;
                const auto cache = vmm::caching::mmio;

                if (const auto ret = vmm::kernel_pagemap->map(_mmio, mmio, npsize, flags, psize, cache); !ret)
                    lib::panic("could not map ioapic mmio: {}", magic_enum::enum_name(ret.error()));

                _redirs = ((read(0x01) >> 16) & 0xFF) + 1;
                for (std::size_t i = 0; i < _redirs; i++)
                    mask(i);
            }

            void set_idx(std::size_t idx, std::uint8_t vector, std::size_t dest, flag flags, delivery deliv) const
            {
                std::uint64_t entry = 0;
                entry |= vector;
                entry |= (std::to_underlying(deliv) & (0b111 << 8));
                entry |= (std::to_underlying(flags) & ~0x7FF);
                entry |= (dest << 56);
                write_entry(idx, entry);
            }

            void mask(std::size_t idx) const
            {
                auto entry = read_entry(idx);
                entry |= (1 << 16);
                write_entry(idx, entry);
            }

            void unmask(std::size_t idx) const
            {
                auto entry = read_entry(idx);
                entry &= ~(1 << 16);
                write_entry(idx, entry);
            }

            std::pair<std::uint32_t, std::uint32_t> gsi_range() const
            {
                return { _gsi_base, _gsi_base + _redirs };
            }
        };
        std::vector<ioapic> ioapics;

        const ioapic &gsi2ioapic(std::uint32_t gsi)
        {
            for (const auto &entry : ioapics)
            {
                auto [start, end] = entry.gsi_range();
                if (start <= gsi && gsi <= end)
                    return entry;
            }
            lib::panic("ioapic: ioapic for gsi {} not found", gsi);
            std::unreachable();
        }

        auto irq2iso(std::uint8_t irq) -> std::optional<std::uint32_t>
        {
            for (const auto &entry : acpi::madt::isos)
            {
                if (static_cast<std::uint8_t>(entry.source) == irq)
                    return static_cast<std::uint32_t>(entry.gsi);
            }
            return std::nullopt;
        }

        bool initialised = false;
    } // namespace

    bool is_initialised() { return initialised; }

    void set_gsi(std::size_t gsi, std::uint8_t vector, std::size_t dest, flag flags, delivery deliv)
    {
        log::debug("ioapic: redirecting gsi {} to vector 0x{:X}", gsi, vector);
        const auto &entry = gsi2ioapic(gsi);
        entry.set_idx(gsi - entry.gsi_range().first, vector, dest, flags, deliv);
    }

    void mask_gsi(std::uint32_t gsi)
    {
        // log::debug("ioapic: masking gsi {}", gsi);
        const auto &entry = gsi2ioapic(gsi);
        entry.mask(gsi - entry.gsi_range().first);
    }

    void unmask_gsi(std::uint32_t gsi)
    {
        // log::debug("ioapic: unmasking gsi {}", gsi);
        const auto &entry = gsi2ioapic(gsi);
        entry.unmask(gsi - entry.gsi_range().first);
    }

    void mask(std::uint8_t vector)
    {
        lib::bug_on(vector < 0x20);

        log::debug("ioapic: masking vector 0x{:X}", vector);
        const auto gsi = irq2iso(vector - 0x20);
        if (gsi.has_value())
            mask_gsi(gsi.value());
        else
            mask_gsi(vector - 0x20);
    }

    void unmask(std::uint8_t vector)
    {
        lib::bug_on(vector < 0x20);

        log::debug("ioapic: unmasking vector 0x{:X}", vector);
        const auto gsi = irq2iso(vector - 0x20);
        if (gsi.has_value())
            unmask_gsi(gsi.value());
        else
            unmask_gsi(vector - 0x20);
    }

    void init()
    {
        log::info("ioapic: setting up");

        if (acpi::madt::hdr == nullptr || acpi::madt::ioapics.empty())
        {
            log::error("ioapic: no ioapics found, falling back to legacy pic");
            return;
        }

        pic::disable();

        for (const auto &entry : acpi::madt::ioapics)
            ioapics.emplace_back(static_cast<std::uintptr_t>(entry.address), static_cast<std::uint32_t>(entry.gsi_base));

        if (acpi::madt::hdr->flags & ACPI_PIC_ENABLED)
        {
            for (std::uint8_t i = 0; i < 16; i++)
            {
                if (i == 2)
                    continue;

                for (const auto &entry : acpi::madt::isos)
                {
                    auto src = static_cast<std::uint8_t>(entry.source);
                    if (src == i)
                    {
                        set_gsi(
                            static_cast<std::uint32_t>(entry.gsi), src + 0x20, cpu::bsp_aid(),
                            static_cast<flag>(entry.flags) | flag::masked, delivery::fixed
                        );
                        if (auto handler = idt::handler_at(cpu::bsp_idx(), src + 0x20))
                            handler.value().get().reserve();
                        goto end;
                    }
                }

                set_gsi(
                    i, i + 0x20, cpu::bsp_aid(),
                    flag::masked, delivery::fixed
                );

                if (auto handler = idt::handler_at(cpu::bsp_idx(), i + 0x20))
                    handler.value().get().reserve();
                end:
            }
        }

        initialised = true;
    }
} // namespace x86_64::apic::io