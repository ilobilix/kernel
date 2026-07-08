// Copyright (C) 2024-2026  ilobilo

module x86_64.drivers.output.e9;

import drivers.output.serial;
import system.cmdline;
import lib;
import std;

namespace x86_64::output::e9
{
    namespace
    {
        void prints(std::string_view str)
        {
            for (const auto chr : str)
                lib::io::out<8>(0xE9, chr);
        }

        constinit lib::logger log {
            prints, [] { }, [] { }
        };
    } // namespace

    void init()
    {
        if (!cmdline::has("debugcon") || lib::io::in<8>(0xE9) != 0xE9)
            return;

        register_logger(&log);
    }
} // namespace x86_64::output::e9
