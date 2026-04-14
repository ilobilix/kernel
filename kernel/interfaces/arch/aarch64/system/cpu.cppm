// Copyright (C) 2024-2025  ilobilo

export module aarch64.system.cpu;
import cppstd;

// TODO: everything

export namespace cpu
{
    struct registers
    {
        std::uint64_t x[31];

        std::uintptr_t fp() { return 0; }
        std::uintptr_t ip() { return 0; }
    };

    struct extra_regs
    {
        static extra_regs read() { return { }; }
    };

    void invlpg(std::uintptr_t) { }

    extern "C++" std::uintptr_t self_addr() { return 0; }
} // export namespace cpu