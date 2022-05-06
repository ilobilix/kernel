// Copyright (C) 2024  ilobilo

module x86_64.system.ioapic;

import system.memory;
import system.acpi;
import system.cpu;
import system.cpu.self;
import lib;
import std;

namespace x86_64::apic::io
{
    namespace
    {
        class ioapic
        {
            private:

            public:
        };
    } // namespace

    void eoi()
    {
        cpu::self()->arch.lapic->eoi();
    }

    void mask(std::uint8_t vector) { }
    void unmask(std::uint8_t vector) { }

    void init()
    {
        log::info("Setting up IOAPIC");

        if (acpi::madt::hdr == nullptr || acpi::madt::ioapics.empty())
        {
            log::error("No IOAPICs found, falling back to legacy PIC");
            return;
        }

        for (const auto &entry : acpi::madt::ioapics)
        {
        }

        // initialised = true;
    }
} // namespace x86_64::apic::io