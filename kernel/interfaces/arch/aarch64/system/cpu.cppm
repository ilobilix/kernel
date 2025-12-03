// Copyright (C) 2024-2025  ilobilo

module;

#include <arch/aarch64/system/cpu.hpp>

export module aarch64.system.cpu;
import std;

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

    void invlpg(std::uintptr_t address)
    {
        asm volatile ("dsb st; tlbi vale1, %0; dsb sy; isb" :: "r"(address >> 12) : "memory");
    }

    void invlasid(std::uintptr_t, std::size_t) { }
    bool has_asids() { return false; }

    void write_el1_base(std::uintptr_t base)
    {
        msr(tpidr_el1, base);
    }

    std::uintptr_t read_el1_base()
    {
        return mrs(tpidr_el1);
    }

    void write_el0_base(std::uintptr_t base)
    {
        msr(tpidr_el0, base);
    }

    std::uintptr_t read_el0_base()
    {
        return mrs(tpidr_el0);
    }

    extern "C++" std::uintptr_t self_addr() { return read_el1_base(); }
} // export namespace cpu