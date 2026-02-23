// Copyright (C) 2024-2026  ilobilo

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
            const auto internal_print = [](std::string_view str)
            {
                for (auto chr : str)
                    output::serial::printc(chr);
#if !ILOBILIX_MAX_UACPI_POINTS
                if (auto ctx = output::term::main())
                    output::term::write(ctx, str);
#endif
            };

            bool first = true;
            for (const auto seg : str | std::views::split('\n'))
            {
                if (!first)
                    internal_print("\r\n");
                first = false;
                internal_print(std::string_view { seg });
            }
        }

        void printc(char chr)
        {
            if (chr == '\n')
                printc('\r');
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