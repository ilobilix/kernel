// Copyright (C) 2024-2025  ilobilo

export module x86_64.system.ioapic;

import magic_enum;
import std;

export namespace x86_64::apic::io
{
    enum class delivery : std::uint32_t
    {
        fixed = (0b000 << 8),
        low_priority = (0b001 << 8),
        smi = (0b010 << 8),
        nmi = (0b100 << 8),
        init = (0b101 << 8),
        ext_int = (0b111 << 8)
    };

    enum class flag : std::uint32_t
    {
        masked = (1 << 16),
        level_sensative = (1 << 15),
        active_low = (1 << 13),
        logical = (1 << 11)
    };
    using magic_enum::bitwise_operators::operator|;

    bool is_initialised();

    void set_gsi(std::size_t gsi, std::uint8_t vector, std::size_t dest, flag flags, delivery deliv);

    void mask_gsi(std::uint32_t gsi);
    void unmask_gsi(std::uint32_t gsi);

    void mask(std::uint8_t vector);
    void unmask(std::uint8_t vector);

    void init();
} // export namespace x86_64::apic::io