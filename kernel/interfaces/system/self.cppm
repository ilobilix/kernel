// Copyright (C) 2024-2025  ilobilo

module;

#include <cerrno>

export module system.cpu.self;

import system.scheduler;
import arch.system;
import lib;
import cppstd;

export namespace cpu
{
    extern "C++"
    {
        struct processor
        {
            // do not move
            processor *self;
            std::uintptr_t stack_top;
            std::uintptr_t initial_pmap;

            std::size_t idx;
            std::size_t arch_id;

            cpu::arch::processor arch;
            sched::percpu sched;

            errno_t err = no_error;
            std::atomic_bool online = false;
        };

        processor *self();
    } // extern "C++"
} // export namespace cpu