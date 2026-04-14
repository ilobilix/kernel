// Copyright (C) 2024-2025  ilobilo

module;

#include <cerrno>

export module system.cpu.self;

import arch.system;
import lib;
import std;

export namespace cpu
{
    extern "C++"
    {
        struct processor
        {
            processor *self;

            std::size_t idx;
            std::size_t arch_id;

            std::uintptr_t stack_top;

            cpu::arch::processor arch;

            errno_t err = no_error;
            std::atomic_bool online = false;
        };

        processor *self();
    } // extern "C++"
} // export namespace cpu