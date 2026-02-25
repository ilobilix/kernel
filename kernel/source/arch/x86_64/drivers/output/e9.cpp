// Copyright (C) 2024-2026  ilobilo

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
        using namespace ::output::serial;
        static constinit logger printer { printc };

        if (lib::io::in<8>(0xE9) == 0xE9)
            register_logger(printer);
    }
} // namespace x86_64::output::e9