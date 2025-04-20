// Copyright (C) 2024-2025  ilobilo

export module system.memory.virt;
export import :pagemap;
export import :mmap;

import magic_enum;
import cppstd;

export namespace vmm
{
    using magic_enum::bitwise_operators::operator~;
    using magic_enum::bitwise_operators::operator&;
    using magic_enum::bitwise_operators::operator&=;
    using magic_enum::bitwise_operators::operator|;
    using magic_enum::bitwise_operators::operator|=;

    enum class space_type : std::size_t
    {
        modules,
        acpi, pci,
        other
    };
    std::uintptr_t alloc_vpages(space_type type, std::size_t pages = 1);

    void init();
} // export namespace vmm