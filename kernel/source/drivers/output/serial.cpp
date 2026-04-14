// Copyright (C) 2024-2026  ilobilo

module drivers.output.serial;

import lib;
import std;

namespace output::serial
{
    namespace
    {
        constinit logger *loggers = nullptr;
    } // namespace

    void register_logger(logger &log)
    {
        if (loggers == nullptr)
        {
            loggers = &log;
            log.next = nullptr;
            return;
        }
        else
        {
            log.next = loggers;
            loggers = &log;
        }
    }

    void printc(char chr)
    {
        auto current = loggers;
        while (current != nullptr)
        {
            current->printc(chr);
            current = current->next;
        }
    }
} // namespace output::serial