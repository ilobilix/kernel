// Copyright (C) 2024-2025  ilobilo

module arch;

import system.cpu;
import cppstd;

namespace arch
{
    [[noreturn]]
    void halt(bool ints)
    {
        if (ints)
        {
            while (true)
                asm volatile ("wfi");
        }
        else asm volatile ("msr daifclr, #0b1111; wfi");
        std::unreachable();
    }

    // TODO
    void halt_others() { }

    void wfi() { asm volatile ("wfi"); }
    void pause() { asm volatile ("isb" ::: "memory"); }

    void int_switch(bool on)
    {
        if (on)
            asm volatile ("msr daifclr, #0b1111");
        else
            asm volatile ("msr daifset, #0b1111");
    }

    bool int_status()
    {
        std::uint64_t val = 0;
        asm volatile ("mrs %0, daif" : "=r"(val));
        return val == 0;
    }

    void dump_regs(cpu::registers *regs, cpu::extra_regs, log::level lvl) { lib::unused(regs, lvl); }

    void init()
    {
        cpu::init();
    }

    namespace core
    {
        void entry(boot::limine_mp_info *cpu) { lib::unused(cpu); }
        void bsp(boot::limine_mp_info *cpu) { lib::unused(cpu); }
    } // namespace core
} // namespace arch
