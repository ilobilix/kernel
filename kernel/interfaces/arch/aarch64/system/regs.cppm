// Copyright (C) 2024-2026  ilobilo

export module system.cpu.regs:impl;
import std;

export namespace cpu
{
    struct registers
    {
        std::uint64_t x[31];

        std::uintptr_t fp() { return 0; }
        std::uintptr_t ip() { return 0; }
    };
} // export namespace cpu