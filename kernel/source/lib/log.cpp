// Copyright (C) 2024-2025  ilobilo

module;

#include <flanterm.h>

module lib;

#if !ILOBILIX_MAX_UACPI_POINTS
import drivers.terminal;
#endif
import drivers.serial;
import system.time;
import std;

namespace log::unsafe
{
    void prints(std::string_view str)
    {
        for (auto chr : str)
            serial::printc(chr);
#if !ILOBILIX_MAX_UACPI_POINTS
        if (auto term = term::main(); term)
            term->write(str);
#endif
    }

    void printc(char chr)
    {
        serial::printc(chr);
#if !ILOBILIX_MAX_UACPI_POINTS
        if (auto term = term::main(); term)
            term->write(chr);
#endif
    }

    extern "C" void putchar_(char chr) { printc(chr); }
} // namespace log::unsafe

namespace log
{
    std::uint64_t get_time()
    {
        const auto clock = time::main_clock();
        if (!clock)
            return 0;

        return clock->ns();
    }
} // namespace log