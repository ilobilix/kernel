// Copyright (C) 2024  ilobilo

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
    std::uintptr_t alloc_vspace(vspace type, std::size_t increment = 0, std::uint16_t alignment = 0);

    void init();
} // export namespace vmm