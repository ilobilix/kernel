// Copyright (C) 2024-2026  ilobilo

module aarch64.drivers.output.pl011;

import drivers.output.serial;
import system.memory;
import arch;
import lib;
import std;

namespace aarch64::output::pl011
{
    namespace
    {
        constexpr std::uintptr_t uart = 0x9000000;
        constinit std::uintptr_t addr = -1;

        constinit lib::spinlock _lock;

        void printc(char chr)
        {
            if (addr == static_cast<std::uintptr_t>(-1)) [[unlikely]]
                return;

            while (lib::mmio::in<16>(addr + 0x18) & (1 << 5))
                arch::pause();

            lib::mmio::out<8>(addr, chr);
        }

        void prints(std::string_view str)
        {
            for (const auto chr : str)
                printc(chr);
        }

        void lock() { _lock.lock(); }
        void unlock() { _lock.unlock(); }

        constinit lib::logger log { prints, lock, unlock };
    } // namespace

    void init()
    {
        const auto psize = vmm::page_size::small;
        const auto len = vmm::pagemap::from_page_size(psize);
        const auto flags = vmm::pflag::rwg;
        const auto caching = vmm::caching::mmio;

        if (const auto ret =
                vmm::kernel_pagemap->map(addr = uart, uart, len, flags, psize, caching);
            !ret)
            lib::panic("could not map uart: {}", lib::error_name(ret.error()));

        // Disable the UART.
        lib::mmio::out<16>(addr + 0x30, 0);

        // Set word length to 8 bits and enable FIFOs
        lib::mmio::out<16>(addr + 0x2C, (3 << 5) | (1 << 4));

        // Enable UART, TX and RX
        lib::mmio::out<16>(addr + 0x30, (1 << 0) | (1 << 8) | (1 << 9));

        register_logger(&log);
    }
} // namespace aarch64::output::pl011
