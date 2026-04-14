// Copyright (C) 2024-2026  ilobilo

#include <cerrno>

import system.cpu.local;
import system.scheduler;

extern "C"
{
    errnos *errno_type::errno_location()
    {
        if (sched::is_initialised())
            return &sched::this_thread()->err;
        return &cpu::self()->err;
    }
} // extern "C"