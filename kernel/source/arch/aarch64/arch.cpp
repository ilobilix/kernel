// Copyright (C) 2024-2025  ilobilo

module arch;

import system.scheduler;
import drivers.timers;
import drivers.output;
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
            cpu::write_el1_base(addr);
            sched::start();
        }
        void bsp(std::uintptr_t addr)
        {
            cpu::write_el1_base(addr);
        }
    } // namespace core
} // namespace arch
