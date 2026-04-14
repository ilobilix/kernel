// Copyright (C) 2024-2025  ilobilo

module system.pci;

import lib;
import std;

namespace pci
{
    struct legacy : configio
    {
        std::uint32_t read(std::uint16_t seg, std::uint8_t bus, std::uint8_t dev, std::uint8_t func, std::size_t offset, std::size_t width) override
        {
            lib::bug_on(seg != 0);

            const std::uint32_t uoff = offset;
            const auto addr =
                (static_cast<std::uint32_t>(bus) << 16) |
                (static_cast<std::uint32_t>(dev) << 11) |
                (static_cast<std::uint32_t>(func) << 8) |
                (static_cast<std::uint32_t>(uoff) & ~(3u)) | 0x80000000u;

            lib::io::out<std::uint32_t>(0xCF8, addr);
            switch (width)
            {
                case sizeof(std::uint8_t):
                    return lib::io::in<std::uint8_t>(0xCFC + (uoff & 3));
                case sizeof(std::uint16_t):
                    return lib::io::in<std::uint16_t>(0xCFC + (uoff & 3));
                case sizeof(std::uint32_t):
                    return lib::io::in<std::uint32_t>(0xCFC + (uoff & 3));
                default:
                    lib::panic("pci::legacy_io::read: invalid width {}", width);
            }
            std::unreachable();
        }

        void write(std::uint16_t seg, std::uint8_t bus, std::uint8_t dev, std::uint8_t func, std::size_t offset, std::uint32_t value, std::size_t width) override
        {
            lib::bug_on(seg != 0);

            const std::uint32_t uoff = offset;
            const auto addr =
                (static_cast<std::uint32_t>(bus) << 16) |
                (static_cast<std::uint32_t>(dev) << 11) |
                (static_cast<std::uint32_t>(func) << 8) |
                (static_cast<std::uint32_t>(uoff) & ~(3u)) | 0x80000000u;

            lib::io::out<std::uint32_t>(0xCF8, addr);
            switch (width)
            {
                case sizeof(std::uint8_t):
                    lib::io::out<std::uint8_t>(0xCFC + (uoff & 3), value);
                    break;
                case sizeof(std::uint16_t):
                    lib::io::out<std::uint16_t>(0xCFC + (uoff & 3), value);
                    break;
                case sizeof(std::uint32_t):
                    lib::io::out<std::uint32_t>(0xCFC + (uoff & 3), value);
                    break;
                default:
                    lib::panic("pci::legacy_io::write: invalid width {}", width);
            }
        }
    };

    namespace acpi
    {
        std::uint32_t ecam_read(std::uintptr_t addr, std::size_t width)
        {
            switch (width)
            {
                case sizeof(std::uint8_t):
                {
                    std::uint8_t val;
                    asm volatile("mov al, [%1]" : "=a"(val) : "r"(reinterpret_cast<volatile std::uint8_t *>(addr)));
                    return val;
                }
                case sizeof(std::uint16_t):
                {
                    std::uint16_t val;
                    asm volatile("mov ax, [%1]" : "=a"(val) : "r"(reinterpret_cast<volatile std::uint16_t *>(addr)));
                    return val;
                }
                case sizeof(std::uint32_t):
                {
                    std::uint32_t val;
                    asm volatile("mov eax, [%1]" : "=a"(val) : "r"(reinterpret_cast<volatile std::uint32_t *>(addr)));
                    return val;
                }
            }
            std::unreachable();
        }

        void ecam_write(std::uintptr_t addr, std::uint32_t value, std::size_t width)
        {
            switch (width)
            {
                case sizeof(std::uint8_t):
                {
                    std::uint8_t val = value;
                    asm volatile ("mov [%1], al" :: "a"(val), "r"(reinterpret_cast<volatile std::uint8_t *>(addr)) : "memory");
                    break;
                }
                case sizeof(std::uint16_t):
                {
                    std::uint16_t val = value;
                    asm volatile ("mov [%1], ax" :: "a"(val), "r"(reinterpret_cast<volatile std::uint16_t *>(addr)) : "memory");
                    break;
                }
                case sizeof(std::uint32_t):
                {
                    std::uint32_t val = value;
                    asm volatile ("mov [%1], eax" :: "a"(val), "r"(reinterpret_cast<volatile std::uint32_t *>(addr)) : "memory");
                    break;
                }
                default:
                    std::unreachable();
            }
        }

        lib::initgraph::stage *ios_discovered_stage();
        lib::initgraph::stage *rbs_discovered_stage();

        extern bool need_arch_ios;
        extern bool need_arch_rbs;
    } // namespace acpi

    namespace arch
    {
        lib::initgraph::stage *ios_discovered_stage();
        lib::initgraph::stage *rbs_discovered_stage();

        lib::initgraph::task ios_task
        {
            "pci.arch.discover-ios",
            lib::initgraph::presched_init_engine,
            lib::initgraph::require { acpi::ios_discovered_stage() },
            lib::initgraph::entail { ios_discovered_stage() },
            [] {
                if (!acpi::need_arch_ios)
                    return;

                auto io = std::make_shared<legacy>();
                for (auto bus : std::views::iota(0, 256))
                    addio(io, 0, bus);
            }
        };

        lib::initgraph::task rbs_task
        {
            "pci.arch.discover-rbs",
            lib::initgraph::presched_init_engine,
            lib::initgraph::require { acpi::rbs_discovered_stage() },
            lib::initgraph::entail { rbs_discovered_stage() },
            [] {
                if (!acpi::need_arch_rbs)
                    return;

                bool at_least_one = false;

                auto io = getio(0, 0);
                if (io->read<8>(0, 0, 0, 0, reg::header) & (1 << 7))
                {
                    for (std::uint8_t i = 0; i < 8; i++)
                    {
                        if (io->read<16>(0, 0, 0, i, reg::venid) == 0xFFFF)
                            continue;

                        at_least_one = true;
                        addrb(std::make_shared<bus>(0, i, getio(0, i)));
                    }
                }

                if (!at_least_one) // hmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm
                    addrb(std::make_shared<bus>(0, 0, io));
            }
        };
    } // namespace arch
} // namespace pci