// Copyright (C) 2024-2025  ilobilo

export module system.cpu;

export import system.cpu.arch;
import std;

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