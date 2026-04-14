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
        constinit std::array<entry, num_ints> idt { };
        constinit reg idtr { };
        constinit bool early = true;

        constexpr std::array<std::string_view, 32> exception_messages
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
            if (apic::io::is_initialised())
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
    > irq_handlers;
    cpu_local_init(irq_handlers);

    [[nodiscard]]
    auto handler_at(std::size_t cpuidx, std::uint8_t num) -> std::optional<std::reference_wrapper<interrupts::handler>>
    {
        if (num < irq(0))
            return std::nullopt;

        num -= irq(0);

        auto &handlers = irq_handlers.get(cpu::nth_base(cpuidx));
        if (num >= handlers.size())
            handlers.resize(std::max(num_ints, static_cast<std::size_t>(num) + 5));

        return handlers[num];
    }

    extern "C" void *isr_table[];
    extern "C" void isr_handler(cpu::registers *regs)
    {
        const auto vector = regs->vector;
        if (early) [[unlikely]]
        {
            lib::panic(regs, "exception {}: '{}'", vector, exception_messages[vector]);
            std::unreachable();
        }

        const auto self = cpu::self();

        if (vector >= irq(0) && vector <= 0xFF)
        {
            const auto idx = vector - irq(0);
            if (irq_handlers->size() > idx)
            {
                auto &handler = irq_handlers[idx];
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

    void init()
    {
        for (std::size_t i = 0; i < num_ints; i++)
            idt[i].set(isr_table[i]);

        idtr.limit = sizeof(idt) - 1;
        idtr.base = reinterpret_cast<std::uintptr_t>(idt.data());
        idtr.load();
    }

    void init_on(cpu::processor *cpu)
    {
        if (cpu->idx == cpu::bsp_idx())
        {
            log::info("idt: setting up irq handlers");

            // page fault ist 0
            idt[14].ist = 1;
        }

        irq_handlers.get(cpu::nth_base(cpu->idx)).resize(num_preints);

        auto phandler = handler_at(cpu->idx, panic_int).value();
        phandler.get().set([](cpu::registers *) { arch::halt(false); });

        if (cpu->idx == cpu::bsp_idx())
            early = false;

        idtr.load();
    }
} // namespace x86_64::idt