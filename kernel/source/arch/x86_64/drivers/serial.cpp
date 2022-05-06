// Copyright (C) 2024  ilobilo

module drivers.serial;

import arch;
import lib;
import std;

namespace serial
{
    namespace
    {
        enum COM
        {
            COM1 = 0x3F8,
            COM2 = 0x2F8,
            COM3 = 0x3E8,
            COM4 = 0x2E8
        };

        using namespace lib::io;

        void printc(char c, COM com)
        {
            while (not (in<std::uint8_t>(com + 5) & (1 << 5)))
                arch::pause();

            out<std::uint8_t>(com, c);
        }

        void init_port(COM com)
        {
            out<std::uint8_t>(com + 1, 0x00);
            out<std::uint8_t>(com + 3, 0x80);
            out<std::uint8_t>(com + 0, 0x01);
            out<std::uint8_t>(com + 1, 0x00);
            out<std::uint8_t>(com + 3, 0x03);
            out<std::uint8_t>(com + 2, 0xC7);
            out<std::uint8_t>(com + 4, 0x0B);
        }
    } // namespace

    void printc(char c) { printc(c, COM1); }
    void early_init() { init_port(COM1); }
    void init() { }
} // namespace serial