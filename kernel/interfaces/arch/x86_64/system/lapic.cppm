// Copyright (C) 2024  ilobilo

export module x86_64.system.lapic;
import std;

export namespace x86_64::apic
{
    enum class timer_mode
    {
        periodic,
        oneshot
    };

    enum class dest
    {
        none = 0b00,
        self = 0b01,
        all = 0b10,
        all_noself = 0b11
    };

    std::pair<bool, bool> supported();

    void eoi();
    void ipi(std::uint8_t id, dest dsh, std::uint8_t vector);

    void init_cpu();
} // export namespace x86_64::apic