// Copyright (C) 2024-2025  ilobilo

export module arch;

import system.cpu;
import boot;
import lib;
import cppstd;

export namespace arch
{
    [[noreturn]]
    void halt(bool ints = true);
    void halt_others();

    void wfi();
    void pause();

    void int_switch(bool on);
    bool int_status();

    inline bool int_switch_status(bool on)
    {
        auto ret = int_status();
        int_switch(on);
        return ret;
    }

    void dump_regs(cpu::registers *regs, cpu::extra_regs eregs, log::level lvl);

    initgraph::stage *bsp_stage()
    {
        static initgraph::stage stage { "arch.bsp-initialised" };
        return &stage;
    }

    initgraph::stage *cpus_stage()
    {
        static initgraph::stage stage { "arch.cpus-initialised" };
        return &stage;
    }

    void early_init();

    namespace core
    {
        void entry(std::uintptr_t addr);
        void bsp(std::uintptr_t addr);
    } // namespace core
} // export namespace arch