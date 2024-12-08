// Copyright (C) 2024  ilobilo

module;

#include <uacpi/acpi.h>

export module system.acpi;
import std;

export namespace acpi
{
    namespace madt
    {
        std::vector<acpi_madt_ioapic> ioapics;
        std::vector<acpi_madt_interrupt_source_override> isos;

        acpi_madt *hdr = nullptr;
    } // namespace madt

    acpi_fadt *fadt = nullptr;

    std::uintptr_t get_rsdp();

    void init();
    void early();
} // export namespace acpi