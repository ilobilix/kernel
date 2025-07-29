// Copyright (C) 2024-2025  ilobilo

module x86_64.drivers.output.e9;

import drivers.output.serial;
import lib;

namespace x86_64::output::e9
{
    void printc(char chr)
    {
        lib::io::out<8>(0xE9, chr);
    }

    void init()
    {
        if (lib::io::in<8>(0xE9) == 0xE9)
            ::output::serial::register_printer(printc);
    }
} // namespace x86_64::output::e9