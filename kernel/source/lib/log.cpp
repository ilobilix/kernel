// Copyright (C) 2024-2025  ilobilo

module;

#include <flanterm.h>

module lib;

#if !ILOBILIX_MAX_UACPI_POINTS
import drivers.output.terminal;
#endif
import drivers.output.serial;
import system.chrono;
import std;

namespace lib::log
{
    namespace unsafe
    {
        void prints(std::string_view str)
        {
            for (auto chr : str)
                output::serial::printc(chr);
#if !ILOBILIX_MAX_UACPI_POINTS
            if (auto ctx = output::term::main())
                output::term::write(ctx, str);
#endif
        }

        void printc(char chr)
        {
            output::serial::printc(chr);
#if !ILOBILIX_MAX_UACPI_POINTS
            if (auto ctx = output::term::main())
                output::term::write(ctx, chr);
#endif
        }

        extern "C" void putchar_(char chr) { printc(chr); }
    } // namespace unsafe

    std::uint64_t get_time()
    {
        const auto clock = chrono::main_clock();
        if (!clock)
            return 0;

        return clock->ns();
    }
} // namespace lib::log