// Copyright (C) 2024-2026  ilobilo

#include <cerrno>

import system.cpu.local;
import system.sched;

extern "C"
{
    errnos *errno_type::errno_location()
    {
        return &sched::current_thread()->err;
    }
} // extern "C"
