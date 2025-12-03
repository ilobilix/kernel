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
    bool in_interrupt();

    inline bool int_switch_status(bool on)
    {
        auto ret = int_status();
        int_switch(on);
        return ret;
    }

    void dump_regs(cpu::registers *regs, cpu::extra_regs eregs, lib::log_level lvl);

    lib::initgraph::stage *bsp_stage()
    {
        static lib::initgraph::stage stage
        {
            "arch.bsp.initialised",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    lib::initgraph::stage *cpus_stage()
    {
        static lib::initgraph::stage stage
        {
            "arch.cpus.initialised",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    void early_init();

    namespace core
    {
        void entry(std::uintptr_t addr);
        void bsp(std::uintptr_t addr);
    } // namespace core
} // export namespace arch