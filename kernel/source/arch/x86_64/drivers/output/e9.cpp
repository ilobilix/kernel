// Copyright (C) 2024-2026  ilobilo

module x86_64.drivers.output.e9;

import drivers.output.serial;
import lib;
import std;

namespace x86_64::output::e9
{
    void prints(std::string_view str)
    {
        for (const auto chr : str)
            lib::io::out<8>(0xE9, chr);
    }

    constinit lib::logger log {
        prints, [] { }, [] { }
    };

    void init()
    {
        if (lib::io::in<8>(0xE9) == 0xE9)
            register_logger(&log);
    }
} // namespace x86_64::output::e9
