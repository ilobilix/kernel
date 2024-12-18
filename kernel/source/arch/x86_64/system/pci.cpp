// Copyright (C) 2024  ilobilo

module system.pci;

import lib;
import std;

namespace pci
{
    struct legacy : configio
    {
        std::uint32_t read(std::uint16_t seg, std::uint8_t bus, std::uint8_t dev, std::uint8_t func, std::size_t offset, std::size_t width) override
        {
            lib::ensure(seg == 0);

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
            lib::ensure(seg == 0);

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

    namespace arch
    {
        void register_ios()
        {
            auto io = std::make_shared<legacy>();
            for (auto bus : std::views::iota(0, 256))
                addio(io, 0, bus);
        }

        void register_rbs()
        {
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
    } // namespace arch
} // namespace pci