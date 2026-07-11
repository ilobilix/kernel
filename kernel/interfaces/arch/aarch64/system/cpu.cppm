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
        std::uint64_t ret;
        asm volatile ((fmt::format("mrs %0, {}"_cf, Reg.value)) : "=r"(ret));
        return ret;
    }

    template<lib::comptime_string Reg, lib::comptime_string Cns = "r">
    void msr(std::uint64_t val)
    {
        asm volatile ((fmt::format("msr {}, %0"_cf, Reg.value)) :: (Cns)(val));
    }

    template<lib::comptime_string Reg>
    std::uint64_t read_reg()
    {
        std::uint64_t ret;
        asm volatile ((fmt::format("mov %0, {}"_cf, Reg.value)) : "=r"(ret));
        return ret;
    }

    template<lib::comptime_string Reg, lib::comptime_string Cns = "r">
    void write_reg(std::uint64_t val)
    {
        asm volatile ((fmt::format("mov {}, %0"_cf, Reg.value)) :: (Cns)(val));
    }

    struct extra_regs
    {
        static extra_regs read() { return { }; }
    };

    namespace tlb
    {
        inline constexpr unsigned asid_bits = 16;

        inline bool _enabled = false;
        inline bool has_asids() { return _enabled; }

        inline void flush_page(std::uintptr_t addr)
        {
            asm volatile ("dsb st; tlbi vale1, %0; dsb sy; isb" :: "r"(addr >> 12) : "memory");
        }

        inline void flush_page(std::uintptr_t addr, std::size_t asid)
        {
            if (!_enabled)
                return flush_page(addr);

            const std::uint64_t op = (addr >> 12) | (static_cast<std::uint64_t>(asid) << 48);
            asm volatile ("dsb ishst; tlbi vae1, %0; dsb ish; isb" :: "r"(op) : "memory");
        }

        inline void flush_asid(std::size_t asid)
        {
            if (!_enabled)
            {
                asm volatile ("dsb ishst; tlbi vmalle1; dsb ish; isb" ::: "memory");
                return;
            }

            const std::uint64_t op = static_cast<std::uint64_t>(asid) << 48;
            asm volatile ("dsb ishst; tlbi aside1, %0; dsb ish; isb" :: "r"(op) : "memory");
        }

        inline void flush_all()
        {
            asm volatile ("dsb ishst; tlbi vmalle1; dsb ish; isb" ::: "memory");
        }
    } // namespace tlb

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

    std::uintptr_t self_addr()
    {
        std::uintptr_t addr;
        asm volatile ("mrs %0, tpidr_el1; ldr %0, [%0]" : "=r"(addr) :: "memory");
        return addr;
    }
} // export namespace cpu
