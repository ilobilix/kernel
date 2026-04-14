// Copyright (C) 2024  ilobilo

module lib;

import drivers.serial;
import system.time;
import std;

namespace log::unsafe
{
    void prints(std::string_view str)
    {
        for (auto chr : str)
            serial::printc(chr);
    }

    void printc(char chr) { serial::printc(chr); }

    extern "C" void putchar_(char chr) { printc(chr); }
} // namespace log::unsafe

namespace log
{
    std::uint64_t get_time()
    {
        auto clock = time::main_clock();
        if (!clock)
            return 0;

        return clock->ns();
    }
} // namespace log