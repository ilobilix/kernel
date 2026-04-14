// Copyright (C) 2024-2025  ilobilo

export module system.memory.virt;
export import :pagemap;
import std;

export namespace vmm
{
    enum class vspace : std::size_t
    {
        drivers,
        acpi, pci,
        other
    };
    std::uintptr_t alloc_vpages(vspace type, std::size_t pages = 1);

    void init();
} // export namespace vmm