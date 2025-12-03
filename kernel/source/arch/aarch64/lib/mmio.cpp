// Copyright (C) 2024-2025  ilobilo

import lib;
import std;

namespace lib::mmio
{
    std::uint64_t read(std::uintptr_t addr, std::size_t width)
    {
        switch (width)
        {
            case sizeof(std::uint8_t):
            {
                volatile auto ptr = reinterpret_cast<volatile std::uint8_t *>(addr);
                return *ptr;
            }
            case sizeof(std::uint16_t):
            {
                volatile auto ptr = reinterpret_cast<volatile std::uint16_t *>(addr);
                return *ptr;
            }
            case sizeof(std::uint32_t):
            {
                volatile auto ptr = reinterpret_cast<volatile std::uint32_t *>(addr);
                return *ptr;
            }
            case sizeof(std::uint64_t):
            {
                volatile auto ptr = reinterpret_cast<volatile std::uint64_t *>(addr);
                return *ptr;
            }
            default:
                lib::panic("lib::mmio::read: invalid width {}", width);
        }
        std::unreachable();
    }

    void write(std::uintptr_t addr, std::uint64_t val, std::size_t width)
    {
        switch (width)
        {
            case sizeof(std::uint8_t):
            {
                volatile auto ptr = reinterpret_cast<volatile std::uint8_t *>(addr);
                *ptr = val;
                break;
            }
            case sizeof(std::uint16_t):
            {
                volatile auto ptr = reinterpret_cast<volatile std::uint16_t *>(addr);
                *ptr = val;
                break;
            }
            case sizeof(std::uint32_t):
            {
                volatile auto ptr = reinterpret_cast<volatile std::uint32_t *>(addr);
                *ptr = val;
                break;
            }
            case sizeof(std::uint64_t):
            {
                volatile auto ptr = reinterpret_cast<volatile std::uint64_t *>(addr);
                *ptr = val;
                break;
            }
            default:
                lib::panic("lib::mmio::write: invalid width {}", width);
        }
    }
} // namespace lib::mmio