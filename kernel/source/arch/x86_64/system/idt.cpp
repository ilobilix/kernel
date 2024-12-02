// Copyright (C) 2024  ilobilo

module x86_64.system.idt;

import x86_64.system.ioapic;
import x86_64.system.pic;
import system.interrupts;
import system.cpu;
import system.cpu.self;
import arch;
import lib;
import std;

namespace x86_64::idt
{
    namespace
    {
        entry idt[num_ints];
        ptr idtr { sizeof(idt) - 1, reinterpret_cast<std::uintptr_t>(&idt) };

        const char *exception_messages[32]
        {
            "Division by zero", "Debug",
            "Non-maskable interrupt",
            "Breakpoint", "Detected overflow",
            "Out-of-bounds", "Invalid opcode",
            "No coprocessor", "Double fault",
            "Coprocessor segment overrun",
            "Bad TSS", "Segment not present",
            "Stack fault", "General protection fault",
            "Page fault", "Unknown interrupt",
            "Coprocessor fault", "Alignment check",
            "Machine check", "Reserved",
            "Reserved", "Reserved", "Reserved",
            "Reserved", "Reserved", "Reserved",
            "Reserved", "Reserved", "Reserved",
            "Reserved", "Reserved", "Reserved"
        };

        void eoi(std::uint8_t vector)
        {
            if (apic::io::initialised)
                apic::io::eoi();
            else
                pic::eoi(vector);
        }
    } // namespace

    std::optional<std::reference_wrapper<interrupts::handler>> handler_at(std::size_t cpuidx, std::uint8_t num)
    {
        if (num < irq(0))
            return std::nullopt;

        num -= irq(0);

        auto &handlers = cpu::processors[cpuidx].arch.int_handlers;
        if (num >= handlers.size())
            handlers.resize(std::max(num_ints, static_cast<std::size_t>(num) + 5));

        return handlers[num];
    }

    extern "C" void *isr_table[];
    extern "C" void isr_handler(cpu::registers *regs)
    {
        if (regs->vector >= irq(0) && regs->vector <= 0xFF)
        {
            auto ptr = cpu::self() ?: &cpu::processors[cpu::bsp_idx];
            auto &handlers = ptr->arch.int_handlers;

            auto idx = regs->vector - irq(0);
            if (handlers.size() > idx)
            {
                auto &handler = handlers[idx];
                if (handler.used())
                    handler(regs);
            }

            eoi(regs->vector);
        }
        else if (regs->vector < irq(0))
            lib::panic(regs, "Exception '{}'", exception_messages[regs->vector]);
        else
            lib::panic(regs, "Unknown interrupt {}", regs->vector);
    }

    void init_on(cpu::processor *cpu)
    {
        if (cpu->idx == cpu::bsp_idx)
        {
            log::info("Setting up IDT");

            for (std::size_t i = 0; i < num_ints; i++)
                idt[i].set(isr_table[i]);

            // page fault ist 2. see TSS
            idt[14].ist = 2;
        }

        cpu->arch.int_handlers.resize(num_preints);

        auto phandler = handler_at(cpu->idx, panic_int).value();
        phandler.get().set([](cpu::registers *) { arch::halt(false); });

        idtr.load();
    }
} // namespace x86_64::idt