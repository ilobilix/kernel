// Copyright (C) 2022-2024  ilobilo

module system.cpu;

import system.cpu.local;
import lib;
import std;

namespace cpu::mp
{
    // TODO
    std::size_t num_cores() { return 1; }
    std::size_t bsp_aid() { return 0; }

    void boot_cores(processor *(*request)(std::size_t))
    {
        if (num_cores() <= 1)
            return;
        lib::unused(request);
    }
} // export namespace cpu::mp