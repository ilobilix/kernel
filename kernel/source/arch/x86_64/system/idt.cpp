// Copyright (C) 2024-2025  ilobilo

module x86_64.system.idt;

import x86_64.system.ioapic;
import x86_64.system.lapic;
import x86_64.system.pic;
import system.interrupts;
import system.cpu.self;
import system.cpu;
import frigg;
import arch;
import lib;
import cppstd;

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

    cpu_local<
        frg::small_vector<
            interrupts::handler, x86_64::idt::num_preints,
            frg::allocator<interrupts::handler>
        >
    > int_handlers;
    cpu_local_init(int_handlers);

    [[nodiscard]]
    auto handler_at(std::size_t cpuidx, std::uint8_t num) -> std::optional<std::reference_wrapper<interrupts::handler>>
    {
        if (num < irq(0))
            return std::nullopt;

        num -= irq(0);

        auto &handlers = int_handlers.get(cpu::nth_base(cpuidx));
        if (num >= handlers.size())
            handlers.resize(std::max(num_ints, static_cast<std::size_t>(num) + 5));

        return handlers[num];
    }

    extern "C" void *isr_table[];
    extern "C" void isr_handler(cpu::registers *regs)
    {
        const auto vector = regs->vector;
        const auto self = cpu::self();

        if (vector >= irq(0) && vector <= 0xFF)
        {
            const auto idx = vector - irq(0);
            if (int_handlers->size() > idx)
            {
                auto &handler = int_handlers[idx];
                if (handler.used())
                    handler(regs);
            }

            eoi(vector);
        }
        else if (vector < irq(0))
        {
            if (self)
                lib::panic(regs, "exception {}: '{}' on cpu {}", vector, exception_messages[vector], self->idx);
            else
                lib::panic(regs, "exception {}: '{}'", vector, exception_messages[vector]);
            std::unreachable();
        }
        else
        {
            lib::panic(regs, "unknown interrupt {}", vector);
            std::unreachable();
        }
    }

    void init_on(cpu::processor *cpu)
    {
        if (cpu->idx == cpu::bsp_idx)
        {
            log::info("idt: setting up and loading");

            for (std::size_t i = 0; i < num_ints; i++)
                idt[i].set(isr_table[i]);

            // page fault ist 0
            idt[14].ist = 1;
            // // scheduler ist 1
            // idt[0xFF].ist = 2;
        }

        int_handlers.get(cpu::nth_base(cpu->idx)).resize(num_preints);

        auto phandler = handler_at(cpu->idx, panic_int).value();
        phandler.get().set([](cpu::registers *) { arch::halt(false); });

        idtr.load();
    }
} // namespace x86_64::idt