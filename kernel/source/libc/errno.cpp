// Copyright (C) 2024-2025  ilobilo

#include <errno.h>

import system.cpu.self;

extern "C"
{
    errno_t *__errno_location()
    {
        return &cpu::self()->err;
    }
} // extern "C"