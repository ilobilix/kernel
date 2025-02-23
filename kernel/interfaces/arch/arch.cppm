// Copyright (C) 2024-2025  ilobilo

export module arch;

import system.cpu;
import boot;
import lib;
import std;

export namespace arch
{
    [[noreturn]]
    void halt(bool ints = true);
    void halt_others();

    void wfi();
    void pause();

    void int_switch(bool on);
    bool int_status();

    void dump_regs(cpu::registers *regs, cpu::extra_regs eregsregs, log::level lvl);

    void init();

    namespace core
    {
        void bsp(boot::limine_mp_info *cpu);
    } // namespace core
} // export namespace arch