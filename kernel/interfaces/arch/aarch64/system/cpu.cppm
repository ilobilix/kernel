// Copyright (C) 2024-2025  ilobilo

export module aarch64.system.cpu;
import std;

// TODO: everything

export namespace cpu
{
    struct registers
    {
        std::uint64_t x[31];
    };

    struct extra_regs
    {
        static extra_regs read() { return { }; }
    };

    void invlpg(std::uintptr_t) { }

    std::uintptr_t arch_self() { return 0; }
} // export namespace cpu