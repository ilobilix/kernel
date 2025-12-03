// Copyright (C) 2024-2025  ilobilo

module;

#include <uacpi/acpi.h>

export module system.acpi;

import lib;
import std;

export namespace acpi
{
#if defined(__x86_64__)
    namespace madt
    {
        std::vector<acpi_madt_lapic> lapics;
        std::vector<acpi_madt_x2apic> x2apics;

        std::vector<acpi_madt_ioapic> ioapics;
        std::vector<acpi_madt_interrupt_source_override> isos;

        acpi_madt *hdr = nullptr;
    } // namespace madt
#endif

    acpi_fadt *fadt = nullptr;

    std::uintptr_t get_rsdp();

    lib::initgraph::stage *tables_stage();
    lib::initgraph::stage *initialised_stage();
    lib::initgraph::stage *workers_stage();
} // export namespace acpi