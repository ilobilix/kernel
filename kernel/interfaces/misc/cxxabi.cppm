// Copyright (C) 2024  ilobilo

export module cxxabi;
import lib;

namespace cxxabi
{
    extern "C" void (*__init_array_start[])();
    extern "C" void (*__init_array_end[])();

    export void construct()
    {
        log::debug("cxxabi: running global constructors");
        for (auto ctor = __init_array_start; ctor < __init_array_end; ctor++)
            (*ctor)();
    }
} // namespace cxxabi