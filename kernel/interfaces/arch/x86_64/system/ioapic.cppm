// Copyright (C) 2024  ilobilo

export module x86_64.system.ioapic;

import magic_enum;
import std;

export namespace x86_64::apic::io
{
    enum class delivery : std::uint8_t
    {
        fixed = 0b000,
        low_priority = 0b001,
        smi = 0b010,
        nmi = 0b100,
        init = 0b101,
        ext_int = 0b111
    };

    enum class flag
    {
        masked = (1 << 0),
        active_low = (1 << 1),
        level_sensative = (1 << 3),
    };
    using magic_enum::bitwise_operators::operator|;

    bool initialised = false;

    void eoi();
    void mask(std::uint8_t vector);
    void unmask(std::uint8_t vector);

    void init();
} // export namespace x86_64::apic::io