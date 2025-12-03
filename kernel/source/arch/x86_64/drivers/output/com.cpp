// Copyright (C) 2024-2025  ilobilo

module x86_64.drivers.output.com;

import drivers.output.serial;
import arch;
import lib;
import std;

namespace x86_64::output::com
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

        void printc(char chr, COM com)
        {
            while (not (lib::io::in<8>(com + 5) & (1 << 5)))
                arch::pause();

            lib::io::out<8>(com, chr);
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

    void printc(char chr)
    {
        printc(chr, COM1);
    }

    void init()
    {
        init_port(COM1);

        using namespace ::output::serial;
        static constinit printer printer { printc };
        register_printer(printer);
    }
} // namespace x86_64::output::com