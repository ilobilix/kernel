// Copyright (C) 2024  ilobilo

#include <frg/macros.hpp>
import lib;

extern "C"
{
    void FRG_INTF(log)(const char *cstring)
    {
        log::debug("Frigg: {}", cstring);
    }

    void FRG_INTF(panic)(const char *cstring)
    {
        lib::panic("Frigg: {}", cstring);
    }
} // extern "C"