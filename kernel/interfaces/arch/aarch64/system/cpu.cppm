// Copyright (C) 2024-2026  ilobilo

export module system.cpu.arch:impl;
export import system.cpu.regs;

import lib;
import fmt;
import std;

// TODO: everything

export namespace cpu
{
    template<lib::comptime_string Reg>
    std::uint64_t mrs()
    {
        using namespace fmt::literals;
        std::uint64_t ret;
        asm volatile ((fmt::format("mrs %0, {}"_cf, Reg.value)) : "=r"(ret));
        return ret;
    }

    template<lib::comptime_string Reg, lib::comptime_string Cns = "r">
    void msr(std::uint64_t val)
    {
        using namespace fmt::literals;
        asm volatile ((fmt::format("msr {}, %0"_cf, Reg.value)) :: (Cns)(val));
    }

    template<lib::comptime_string Reg>
    std::uint64_t read_reg()
    {
        using namespace fmt::literals;
        std::uint64_t ret;
        asm volatile ((fmt::format("mov %0, {}"_cf, Reg.value)) : "=r"(ret));
        return ret;
    }

    template<lib::comptime_string Reg, lib::comptime_string Cns = "r">
    void write_reg(std::uint64_t val)
    {
        using namespace fmt::literals;
        asm volatile ((fmt::format("mov {}, %0"_cf, Reg.value)) :: (Cns)(val));
    }

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
        msr<"tpidr_el1">(base);
    }

    std::uintptr_t read_el1_base()
    {
        return mrs<"tpidr_el1">();
    }

    void write_el0_base(std::uintptr_t base)
    {
        msr<"tpidr_el0">(base);
    }

    std::uintptr_t read_el0_base()
    {
        return mrs<"tpidr_el0">();
    }

    std::uintptr_t self_addr() { return read_el1_base(); }
} // export namespace cpu
