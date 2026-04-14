// Copyright (C) 2024  ilobilo

module drivers.serial;

import system.memory;
import arch;
import lib;
import std;

namespace serial
{
    namespace
    {
        constexpr std::uintptr_t uart = 0x9000000;
        std::uintptr_t addr = -1;
    } // namespace

    void printc(char c)
    {
        if (addr == static_cast<std::uintptr_t>(-1)) [[unlikely]]
            return;

        while (lib::mmio::in<16>(addr + 0x18) & (1 << 5))
            arch::pause();

        lib::mmio::out<8>(addr, c);
    }

    void early_init() { }

    void init()
    {
        // TODO: map
        addr = lib::tohh(uart);

        // Disable the UART.
        lib::mmio::out<16>(addr + 0x30, 0);

        // Set word length to 8 bits and enable FIFOs
        lib::mmio::out<16>(addr + 0x2C, (3 << 5) | (1 << 4));

        // Enable UART, TX and RX
        lib::mmio::out<16>(addr + 0x30, (1 << 0) | (1 << 8) | (1 << 9));
    }
} // namespace serial