// Copyright (C) 2024-2026  ilobilo

module arch;

import system.memory.tlb;
import system.cpu.local;
import system.sched;
import drivers.timers;
import drivers.output;

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

    void dump_regs(std::size_t idx, cpu::registers *regs, cpu::extra_regs eregs)
    {
        lib::unused(idx, regs, eregs);
    }

    std::uint64_t cycle_count() { return cpu::mrs<"cntvct_el0">(); }

    // TODO: ID_AA64ISAR0_EL1.RNDR
    std::size_t hardware_random(std::span<std::byte> out)
    {
        lib::unused(out);
        return 0;
    }

    void early_init() { }

    lib::initgraph::task bsp_task {
        "arch.bsp.initialise", lib::initgraph::presched_init_engine,
        lib::initgraph::require { lib::initgraph::base_stage() },
        lib::initgraph::entail { bsp_initialised_stage() }, [] { cpu::init_bsp(); }
    };

    lib::initgraph::task cpus_task {
        "arch.cpus.initialise", lib::initgraph::presched_init_engine,
        lib::initgraph::require { bsp_initialised_stage(), timers::initialised_stage() },
        lib::initgraph::entail { cpus_stage() }, [] { cpu::init(); }
    };

    namespace core
    {
        void entry(std::uintptr_t addr)
        {
            auto ptr = reinterpret_cast<cpu::processor *>(addr);
            cpu::write_el1_base(addr);
            tlb::init_cpu(ptr->idx);
            ptr->online = true;
            sched::start();
        }

        void bsp(std::uintptr_t addr)
        {
            auto ptr = reinterpret_cast<cpu::processor *>(addr);
            cpu::write_el1_base(addr);
            tlb::init_cpu(ptr->idx);
            ptr->online = true;
        }
    } // namespace core
} // namespace arch
