// Copyright (C) 2024-2025  ilobilo

module;

#include <uacpi/acpi.h>

module system.interrupts;

import x86_64.system.ioapic;
import x86_64.system.pic;
import x86_64.system.idt;
import system.acpi;
import lib;
import std;

namespace interrupts
{
    using namespace x86_64;

    std::optional<std::pair<handler &, std::size_t>> allocate(std::size_t cpuidx, std::size_t hint)
    {
        lib::bug_on(hint >= idt::num_ints || cpuidx >= cpu::count());

        if (hint < idt::irq(0))
            hint += idt::irq(0);

        if ((acpi::madt::hdr ? (acpi::madt::hdr->flags & ACPI_PIC_ENABLED) : true) && (hint >= idt::irq(0) && hint <= idt::irq(15)))
        {
            auto handler = idt::handler_at(cpuidx, hint).value();
            if (handler.get().used() == false)
                return std::make_pair(handler, hint);
        }

        for (std::size_t i = hint; i < idt::num_ints; i++)
        {
            auto handler = idt::handler_at(cpuidx, i).value();
            if (handler.get().used() == false && handler.get().is_reserved() == false)
            {
                handler.get().reserve();
                return std::make_pair(handler, i);
            }
        }
        return std::nullopt;
    }

    std::optional<std::reference_wrapper<handler>> get(std::size_t cpuidx, std::size_t vector)
    {
        lib::bug_on(vector >= idt::num_ints || cpuidx >= cpu::count());
        return idt::handler_at(cpuidx, vector);
    }

    void mask(std::size_t vector)
    {
        lib::bug_on(vector < idt::irq(0) || vector >= idt::num_ints);

        if (apic::io::is_initialised())
            apic::io::mask(vector);
        else
            pic::mask(vector);
    }

    void unmask(std::size_t vector)
    {
        lib::bug_on(vector < idt::irq(0) || vector >= idt::num_ints);

        if (apic::io::is_initialised())
            apic::io::unmask(vector);
        else
            pic::unmask(vector);
    }
} // export namespace interrupts