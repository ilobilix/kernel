// Copyright (C) 2024-2025  ilobilo

module drivers.output.serial;

import lib;
import std;

namespace output::serial
{
    namespace
    {
        constinit printer *printers = nullptr;
    } // namespace

    void register_printer(printer &prn)
    {
        if (printers == nullptr)
        {
            printers = &prn;
            prn.next = nullptr;
            return;
        }
        else
        {
            prn.next = printers;
            printers = &prn;
        }
    }

    void printc(char chr)
    {
        auto current = printers;
        while (current != nullptr)
        {
            current->printc(chr);
            current = current->next;
        }
    }
} // namespace output::serial