// Copyright (C) 2024-2025  ilobilo

module drivers.output.serial;
import cppstd;
import lib;

namespace output::serial
{
    namespace
    {
        // eeeeehhhhhhhhhhhhhhhhh
        std::array<printer, 5> printers;
        std::size_t idx = 0;
    } // namespace

    void register_printer(printer prn)
    {
        lib::ensure(idx < printers.size());
        printers[idx++] = prn;
    }

    void printc(char chr)
    {
        for (auto prn : printers)
        {
            if (prn)
                (*prn)(chr);
        }
    }
} // namespace output::serial