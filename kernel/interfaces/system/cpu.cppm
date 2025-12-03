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
    namespace local
    {
        extern "C++" bool available();

        processor *nth(std::size_t n);
        std::uintptr_t nth_base(std::size_t n);
    } // namespace local

    std::size_t bsp_idx();
    std::size_t bsp_aid();
    std::size_t count();

    void init_bsp();
    void init();
} // export namespace cpu