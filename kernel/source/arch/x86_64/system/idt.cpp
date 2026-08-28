// Copyright (C) 2024-2026  ilobilo

module x86_64.system.idt;

import x86_64.system.vector;
import system.cpu.regs;
import system.cpu;
import lib;

namespace x86_64::idt
{
    namespace
    {
        constinit std::array<entry, vector::num_ints> idt { };
        constinit reg idtr { };
    } // namespace

    std::array<entry, vector::num_ints> &table() { return idt; }

    extern "C" void *isr_table[];
    extern "C" void isr_handler(cpu::registers *regs)
    {
        vector::dispatch(regs);
    }

    void init()
    {
        for (std::size_t i = 0; i < vector::num_ints; i++)
            idt[i].set(isr_table[i]);

        idtr.limit = sizeof(idt) - 1;
        idtr.base = reinterpret_cast<std::uintptr_t>(idt.data());
        idtr.load();
    }

    void init_on(cpu::processor *cpu)
    {
        if (cpu->idx == cpu::bsp_idx())
        {
            idt[2].ist = 1;
            idt[8].ist = 2;
            idt[18].ist = 3;
        }
        idtr.load();
    }
} // namespace x86_64::idt
