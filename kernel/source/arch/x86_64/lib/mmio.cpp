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
                std::uint8_t value;
                asm volatile ("mov %0, %1" : "=q"(value) : "m"(*reinterpret_cast<volatile std::uint8_t *>(addr)) : "memory");
                return value;
            }
            case sizeof(std::uint16_t):
            {
                std::uint16_t value;
                asm volatile ("mov %0, %1" : "=r"(value) : "m"(*reinterpret_cast<volatile std::uint16_t *>(addr)) : "memory");
                return value;
            }
            case sizeof(std::uint32_t):
            {
                std::uint32_t value;
                asm volatile ("mov %0, %1" : "=r"(value) : "m"(*reinterpret_cast<volatile std::uint32_t *>(addr)) : "memory");
                return value;
            }
            case sizeof(std::uint64_t):
            {
                std::uint64_t value;
                asm volatile ("mov %0, %1" : "=r"(value) : "m"(*reinterpret_cast<volatile std::uint64_t *>(addr)) : "memory");
                return value;
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
                asm volatile ("mov %1, %0" :: "q"(val), "m"(*reinterpret_cast<volatile std::uint8_t *>(addr)) : "memory");
                break;
            case sizeof(std::uint16_t):
                asm volatile ("mov %1, %0" :: "r"(val), "m"(*reinterpret_cast<volatile std::uint16_t *>(addr)) : "memory");
                break;
            case sizeof(std::uint32_t):
                asm volatile ("mov %1, %0" :: "r"(val), "m"(*reinterpret_cast<volatile std::uint32_t *>(addr)) : "memory");
                break;
            case sizeof(std::uint64_t):
                asm volatile ("mov %1, %0" :: "r"(val), "m"(*reinterpret_cast<volatile std::uint64_t *>(addr)) : "memory");
                break;
            default:
                lib::panic("lib::mmio::write: invalid width {}", width);
        }
    }
} // namespace lib::mmio