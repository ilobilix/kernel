// Copyright (C) 2024-2025  ilobilo

module x86_64.system.idt;

import x86_64.system.ioapic;
import x86_64.system.lapic;
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
        std::array<entry, num_ints> idt;
        const ptr idtr {
            sizeof(idt) - 1,
            reinterpret_cast<std::uintptr_t>(idt.data())
        };

        std::array<const char *, 32> exception_messages
        {
            "division by zero", "debug",
            "non-maskable interrupt",
            "breakpoint", "detected overflow",
            "out-of-bounds", "invalid opcode",
            "no coprocessor", "double fault",
            "coprocessor segment overrun",
            "bad TSS", "segment not present",
            "stack fault", "general protection fault",
            "page fault", "unknown interrupt",
            "coprocessor fault", "alignment check",
            "machine check", "reserved",
            "reserved", "reserved", "reserved",
            "reserved", "reserved", "reserved",
            "reserved", "reserved", "reserved",
            "reserved", "reserved", "reserved"
        };

        void eoi(std::uint8_t vector)
        {
            if (apic::io::initialised)
                apic::eoi();
            else
                pic::eoi(vector);
        }
    } // namespace

    [[nodiscard]]
    auto handler_at(std::size_t cpuidx, std::uint8_t num) -> std::optional<std::reference_wrapper<interrupts::handler>>
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
            const auto ptr = cpu::self() ?: &cpu::processors[cpu::bsp_idx];
            auto &handlers = ptr->arch.int_handlers;

            const auto idx = regs->vector - irq(0);
            if (handlers.size() > idx)
            {
                auto &handler = handlers[idx];
                if (handler.used())
                    handler(regs);
            }

            eoi(regs->vector);
        }
        else if (regs->vector < irq(0))
            lib::panic(regs, "exception {}: '{}'", regs->vector, exception_messages[regs->vector]);
        else
            lib::panic(regs, "unknown interrupt {}", regs->vector);
    }

    void init_on(cpu::processor *cpu)
    {
        if (cpu->idx == cpu::bsp_idx)
        {
            log::info("idt: setting up and loading");

            for (std::size_t i = 0; i < num_ints; i++)
                idt[i].set(isr_table[i]);

            // page fault ist 0. see TSS
            idt[14].ist = 1;
        }

        cpu->arch.int_handlers.resize(num_preints);

        auto phandler = handler_at(cpu->idx, panic_int).value();
        phandler.get().set([](cpu::registers *) { arch::halt(false); });

        idtr.load();
    }
} // namespace x86_64::idt