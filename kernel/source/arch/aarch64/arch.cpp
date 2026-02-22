// Copyright (C) 2024-2026  ilobilo

module arch;

import system.scheduler;
import drivers.timers;
import drivers.output;
import system.cpu;
import system.cpu.local;
import std;

namespace arch
{
    [[noreturn]]
    void halt(bool ints)
    {
        if (ints)
        {
            while (true)
                wfi();
        }
        else
        {
            cpu::msr<"daifclr", "i">(0b1111);
            wfi();
        }
        std::unreachable();
    }

    // TODO
    void halt_others() { }

    void wfi() { asm volatile ("wfi"); }
    void pause() { asm volatile ("isb" ::: "memory"); }

    void int_switch(bool on)
    {
        if (on)
            cpu::msr<"daifclr", "i">(0b1111);
        else
            cpu::msr<"daifset", "i">(0b1111);
    }

    bool int_status()
    {
        return cpu::mrs<"daif">() == 0;
    }

    void dump_regs(cpu::registers *regs, cpu::extra_regs, lib::log_level lvl)
    {
        lib::unused(regs, lvl);
    }

    void early_init() { }

    lib::initgraph::task bsp_task
    {
        "arch.bsp.initialise",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { lib::initgraph::base_stage() },
        lib::initgraph::entail { bsp_stage() },
        [] {
            cpu::init_bsp();
        }
    };

    lib::initgraph::task cpus_task
    {
        "arch.cpus.initialise",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { bsp_stage(), timers::initialised_stage() },
        lib::initgraph::entail { cpus_stage() },
        [] {
            cpu::init();
        }
    };

    namespace core
    {
        void entry(std::uintptr_t addr)
        {
            auto ptr = reinterpret_cast<cpu::processor *>(addr);
            cpu::write_el1_base(addr);
            ptr->online = true;
            sched::start();
        }

        void bsp(std::uintptr_t addr)
        {
            auto ptr = reinterpret_cast<cpu::processor *>(addr);
            cpu::write_el1_base(addr);
            ptr->online = true;
        }
    } // namespace core
} // namespace arch
