// Copyright (C) 2024-2025  ilobilo

#include <frg/macros.hpp>
import lib;

extern "C"
{
    void FRG_INTF(log)(const char *cstring)
    {
        lib::debug("frigg: {}", cstring);
    }

    void FRG_INTF(panic)(const char *cstring)
    {
        lib::panic("frigg: {}", cstring);
    }
} // extern "C"