// Copyright (C) 2024-2025  ilobilo

module drivers.serial;

import arch;
import lib;
import cppstd;

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
        bool e9 = false;

        void printc(char c, COM com)
        {
            if (e9)
                lib::io::out<8>(0xE9, c);

            while (not (lib::io::in<8>(com + 5) & (1 << 5)))
                arch::pause();

            lib::io::out<8>(com, c);
        }

        void init_port(COM com)
        {
            lib::io::out<8>(com + 1, 0x00);
            lib::io::out<8>(com + 3, 0x80);
            lib::io::out<8>(com + 0, 0x01);
            lib::io::out<8>(com + 1, 0x00);
            lib::io::out<8>(com + 3, 0x03);
            lib::io::out<8>(com + 2, 0xC7);
            lib::io::out<8>(com + 4, 0x0B);
        }
    } // namespace

    void printc(char c) { printc(c, COM1); }
    void early_init()
    {
        if (lib::io::in<8>(0xE9) == 0xE9)
            e9 = true;

        init_port(COM1);
    }
    void init() { }
} // namespace serial