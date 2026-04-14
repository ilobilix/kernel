// Copyright (C) 2024-2025  ilobilo

export module cxxabi;
import lib;

namespace cxxabi
{
    extern "C" void (*__start_init_array[])();
    extern "C" void (*__end_init_array[])();

    export void construct()
    {
        lib::debug("cxxabi: running global constructors");
        for (auto ctor = __start_init_array; ctor < __end_init_array; ctor++)
            (*ctor)();
    }
} // namespace cxxabi