// Copyright (C) 2024-2025  ilobilo

export module system.cpu;

export import system.cpu.arch;
import std;

export namespace cpu
{
    std::size_t bsp_idx();
    std::size_t bsp_aid();
    std::size_t count();

    void init_bsp();
    void init();
} // export namespace cpu