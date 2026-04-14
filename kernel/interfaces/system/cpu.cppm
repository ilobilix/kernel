// Copyright (C) 2024-2025  ilobilo

export module system.cpu;
import std;

#if defined(__x86_64__)
export import x86_64.system.cpu;
#elif defined(__aarch64__)
export import aarch64.system.cpu;
#else
#  error "Unsupported architecture"
#endif

export namespace cpu
{
    extern "C++" struct processor;
    processor *processors;

    std::size_t bsp_idx;
    std::size_t bsp_aid;
    std::size_t cpu_count;

    void init_bsp();
    void init();
} // export namespace cpu